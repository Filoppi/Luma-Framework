//FFXV_Directional_Light.hlsl 0x2100CE9B

//from luma
#include "../Includes/Common.hlsl"

//|||||||||||||||||||||||||| CONFIGURATION - MICRO SHADOWS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| CONFIGURATION - MICRO SHADOWS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| CONFIGURATION - MICRO SHADOWS ||||||||||||||||||||||||||

//simulates micro-level shadowing on materials (using material ao) super cheap and performant! (from uncharted 4)
//but this can lead to some materials/objects looking much darker than usual.
//if this is not desired you can just disable to revert to (mostly) original shading
#define ENABLE_MICRO_SHADOWS

#define MICRO_SHADOWS_STRENGTH 1.0

//|||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||

#define ENABLE_CONTACT_SHADOWS

// Ray-march quality. Larger values reduce gaps/noise at a proportional cost.
#define CONTACT_SHADOWS_SAMPLES 16

// View-space length of the ray toward the directional light.
#define CONTACT_SHADOWS_RAY_LENGTH 0.5f

// Stratifies one depth lookup inside each ray-march interval. The captured
// buffers expose no reliable frame index, so this is stable spatial noise.
// Set the strength to zero for deterministic interval midpoints.
#define CONTACT_SHADOWS_INTERLEAVED_GRADIENT_NOISE
#define CONTACT_SHADOWS_NOISE_STRENGTH 1.0f

// Maximum view-space thickness accepted as an occluder. FFXV uses meters;
// 0.00325 m matches the 0.325 cm cap used by the Rebirth implementation.
#define CONTACT_SHADOWS_THICKNESS 0.15f

// Uses a finite ray-depth segment and grows thickness from a small minimum by
// the sampled pixel's view-space footprint. CONTACT_SHADOWS_THICKNESS times
// CONTACT_SHADOWS_THICKNESS_SCALE remains the hard maximum.
#define CONTACT_SHADOWS_IMPROVED_THICKNESS
#define CONTACT_SHADOWS_MIN_THICKNESS 0.00065f
#define CONTACT_SHADOWS_PIXEL_THICKNESS_SCALE 25.0f

// Ignore the receiver-adjacent part of the ray. Grazing rays receive a larger
// exclusion because they remain close to the launching surface for longer.
#define CONTACT_SHADOWS_SELF_OCCLUSION_SKIP_STEPS 0.5f
#define CONTACT_SHADOWS_GRAZING_EXTRA_SKIP_STEPS 1.0f

// Screen-space rays shorter than this cannot be represented reliably by the
// depth buffer and commonly cause uniform darkening on distant geometry.
#define CONTACT_SHADOWS_MIN_SCREEN_RAY_LENGTH_PIXELS 1.75f

// The depth-comparison bias and view-space normal offset.
#define CONTACT_SHADOWS_BIAS 0.001f
#define CONTACT_SHADOWS_NORMAL_BIAS 0.001f

// Keeps the configured biases equivalent at 1080p, larger at lower
// resolutions, and smaller at higher resolutions.
#define CONTACT_SHADOWS_BIAS_REFERENCE_HEIGHT 1080.0f

// Overall strength applied to the directional-light visibility.
#define CONTACT_SHADOWS_STRENGTH 1.0f

// Legacy point-test multiplier. Keep this at one now that thickness is
// expressed directly in FFXV view-space meters.
#define CONTACT_SHADOWS_THICKNESS_SCALE 1.0f

// Gradually weakens occlusion found farther along the light ray.
//#define CONTACT_SHADOWS_FALLOFF
#define CONTACT_SHADOWS_FALLOFF_CONTRAST 3.0f

// Converts the reconstructed projection coordinate into texture UV movement.
// This is retained as a fallback for a degenerate interpolant/UV derivative
// basis. Normal contact-shadow projection uses the full homogeneous param3.
#define CONTACT_SHADOWS_NDC_TO_UV_SCALE float2(0.5f, 0.5f)

// Prevents projection through the camera plane.
#define CONTACT_SHADOWS_MIN_VIEW_DEPTH 0.1f

#define USE_FAST_NOISE 1

//|||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| MATH ||||||||||||||||||||||||||

static const float PI_RCP                  = 0.318310f;

static const float VISIBILITY_SCALE        = 0.398942f;
static const float MIN_ROUGHNESS           = 0.050000f;
static const float SPECULAR_ALPHA_SCALE    = 0.160630f;
static const float SPECULAR_ALPHA_MID_BIAS = 0.080630f;

//|||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| STRUCTS ||||||||||||||||||||||||||

//the DXBC declares a 12-byte structured element and loads the float at byte offset 4 from element zero. 
//the original names of the other fields are lost.
struct ExposureValues
{
    float unknown0; //float exposure_;
    float exposure; //float exposureScale_;
    float unknown2; //float maxExposure_;
};

struct PointLightShapeParameter
{
    float3 vpos;
    float  range;
};

struct PointLightParameter
{
    float3 color;
    float  radius;

    float shadowPower;
    float saoPower;
    float roughnessModifier;
    float invRangeSquaredAndSmoothFalloff;

    float  profileIndex;
    float  shadowCoordScale;
    float2 shadowCoordOffset;

    float specRadius;
    uint  depthRange;
    float shadowPowerHair;
    float padding;
};

struct MaterialData
{
    float3 diffuseColor;
    float3 specularF0;
    float3 backscatterColor;
    float  metallicFactor;
    float  perceptualRoughness;

    bool useSpecialBackscatterFresnel;
    bool attenuateSpecularForAlphaMode;
    bool writeSpecularSeparately;
};

struct FresnelData
{
    float3 value;
    float  specularScale;
    float  negativeNdotLScale;
};

struct LightLobes
{
    float3 specular;
    float3 diffuse;
};

//param3 is declared as a perspective-correct interpolant.
//the rasterizer divides it by the same reciprocal-W stored in SV_Position.w. 
//multiplying the two restores the affine homogeneous quantity before taking derivatives.
//keeping all three components is important: param3.xy / param3.z is a projective mapping, not an affine screen coordinate.
struct ContactShadowProjectionContext
{
    float2 receiverUV;
    float2 receiverProjectionCoordinate;
    float3 homogeneousParam3;
    float3 homogeneousParam3Dx;
    float3 homogeneousParam3Dy;
    float  reciprocalW;
    float  reciprocalWDx;
    float  reciprocalWDy;
    float2 homogeneousUV;
    float2 homogeneousUVDx;
    float2 homogeneousUVDy;
};

struct InputStruct
{
    float4 Position      : SV_Position;
    sample float2 param1 : TEXCOORD0;
    sample float4 param2 : TEXCOORD1;
    sample float3 param3 : TEXCOORD2;
};

struct OutputStruct
{
    float4 Target0 : SV_Target0;
    float4 Target1 : SV_Target1;
};

//|||||||||||||||||||||||||| SHADER RESOURCES ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER RESOURCES ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER RESOURCES ||||||||||||||||||||||||||

SamplerState pointClampSampler : register(s0);

StructuredBuffer<ExposureValues> ExposureBuffer : register(t0);

Texture2D<float4> albedoSampler                  : register(t1);
Texture2D<float4> specularSampler                : register(t2);
Texture2D<float4> normalSampler                  : register(t3);
Texture2D<float>  depthSampler                   : register(t4);
Texture2D<float>  shadowSampler                  : register(t5);
#if USE_FAST_NOISE
Texture2DArray<float4> noiseTexture               : register(t42);
#endif
//|||||||||||||||||||||||||| SHADER CBUFFERS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER CBUFFERS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SHADER CBUFFERS ||||||||||||||||||||||||||

cbuffer _Globals : register(b0)
{
    float4   shadowProjMatrix0  : packoffset(c0.x);
    float4   shadowProjMatrix1  : packoffset(c1.x);
    float4   shadowProjMatrix2  : packoffset(c2.x);
    float4   shadowProjMatrix3  : packoffset(c3.x);
    float4   shadowFadeParam    : packoffset(c4.x);
    float    shadowFadeFarValue : packoffset(c5.x);
    float4   shadowDimensions   : packoffset(c6.x);
    float    shadowNormalBias   : packoffset(c7.x);
    float4   shadowShifts[4]    : packoffset(c8.x);
    float4   shadowMapCubeDepthParam : packoffset(c12.x);
    float4   projMatrixZ        : packoffset(c13.x);
    bool     backscatterAttenuation : packoffset(c14.x);
    float    backscatterSpecularColor : packoffset(c14.y);
    float4   lightColor         : packoffset(c15.x);
    float4   lightDir           : packoffset(c16.x);

    //row_major makes matrix[i] address the contiguous c-register shown in the DXBC (c17+i and c22+i respectively).
    row_major float4x4 projInvMatrix : packoffset(c17.x);

    float4   projInvMatrixZ     : packoffset(c21.x);

    row_major float4x4 viewMatrix    : packoffset(c22.x);
    
    float4   projExtentsZ       : packoffset(c26.x);
    float4   projExtentsXY      : packoffset(c27.x);
};

cbuffer cb1 : register(b1)
{
    PointLightShapeParameter g_lightShapeParam : packoffset(c0.x);
    PointLightParameter      g_lightParam      : packoffset(c1.x);
};

//|||||||||||||||||||||||||| RANDOM ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| RANDOM ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| RANDOM ||||||||||||||||||||||||||

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

//|||||||||||||||||||||||||| DECODING ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| DECODING ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| DECODING ||||||||||||||||||||||||||

//decodes the octahedral normal packed across normalTexel.rgb. 
//the unusual constants and the small floor-dependent correction are preserved verbatim.
float3 DecodePackedNormal(float4 normalTexel)
{
    float packedSlice = normalTexel.y * 15.937500f;

    float2 oct;
    oct.x = normalTexel.x * 1.992674f + floor(packedSlice) * 0.000488f;
    oct.y = frac(packedSlice) * 2.000489f + normalTexel.z * 0.124542f;
    oct -= 1.0f;

    float3 normal = float3(oct, 1.0f - abs(oct.x) - abs(oct.y));

    if (normal.z < 0.0f)
        normal.xy = (1.0f - abs(oct.yx)) * float2(oct.x >= 0.0f ? 1.0f : -1.0f, oct.y >= 0.0f ? 1.0f : -1.0f);

    return normalize(normal);
}

//the alpha and blue channels both use a triangular 0..1..0 decode, with slightly asymmetric constants matching the captured instructions.
float DecodeTriangularAlpha(float encoded)
{
    return encoded * SPECULAR_ALPHA_SCALE + (encoded > 0.5f ? -SPECULAR_ALPHA_MID_BIAS : -0.0f);
}

float DecodeTriangularBlue(float encoded)
{
    return encoded * 2.007874f + (encoded > 0.5f ? -1.007874f : -0.0f);
}

MaterialData DecodeMaterial(float4 albedoTexel, float4 specularTexel, float4 normalTexel)
{
    MaterialData material;

    const bool reservedZeroOneZero = specularTexel.r == 0.0f && specularTexel.g == (1.0f / 255.0f) && specularTexel.b == 0.0f;
    const bool zeroGreenZeroBlueMode = specularTexel.g == 0.0f && specularTexel.b == 0.0f;

    material.writeSpecularSeparately = specularTexel.g == 0.0f && specularTexel.b != 0.0f;
    material.useSpecialBackscatterFresnel = specularTexel.g != 0.0f && !reservedZeroOneZero && specularTexel.b <= 0.5f;
    material.attenuateSpecularForAlphaMode = specularTexel.a == 0.0f || specularTexel.a == (128.0f / 255.0f);

    //only the zero-green/zero-blue mode treats specular.r as a metallic-like interpolation factor. 
    //in all other modes this value is exactly zero.
    material.metallicFactor = zeroGreenZeroBlueMode ? specularTexel.r : 0.0f;
    material.diffuseColor = (1.0f - material.metallicFactor) * albedoTexel.rgb;

    if (material.writeSpecularSeparately)
        material.diffuseColor = 1.0f;

    const float dielectricF0 = DecodeTriangularAlpha(specularTexel.a);

    material.specularF0 = lerp(dielectricF0.xxx, albedoTexel.rgb, material.metallicFactor);
    material.backscatterColor = specularTexel.g > 0.0f ? float3(specularTexel.r, specularTexel.g, DecodeTriangularBlue(specularTexel.b)) : 0.0f.xxx;
    material.perceptualRoughness = saturate(normalTexel.a);

    return material;
}

//|||||||||||||||||||||||||| BRDF ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| BRDF ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| BRDF ||||||||||||||||||||||||||

//this is the captured microfacet denominator/visibility approximation. 
//it is intentionally left in the shader's original algebra rather than replaced by a named textbook BRDF, since doing that would change the result.
float EvaluateSpecularBRDF(float roughnessAlpha, float NdotH, float positiveNdotL, float NdotV)
{
    const float alphaSquared = roughnessAlpha * roughnessAlpha;
    const float visibilityFloor = roughnessAlpha * VISIBILITY_SCALE;
    const float visibilitySlope = 1.0f - visibilityFloor;
    const float distributionBase = 1.0f + NdotH * NdotH * (alphaSquared - 1.0f);
    const float visibilityL = positiveNdotL * visibilitySlope + visibilityFloor;
    const float visibilityV = saturate(NdotV) * visibilitySlope + visibilityFloor;

    return (0.25f * alphaSquared) / (distributionBase * distributionBase * visibilityL * visibilityV);
}

FresnelData EvaluateFresnel(MaterialData material, float3 albedo, float LdotH)
{
    FresnelData result;

    const float3 colorAttenuation = lerp(1.0f.xxx, albedo, backscatterSpecularColor);

    float3 f0 = material.specularF0;
    float3 grazingColor = 1.0f.xxx;
    result.specularScale = 1.0f;
    result.negativeNdotLScale = 1.0f;

    if (material.useSpecialBackscatterFresnel)
    {
        if (!backscatterAttenuation)
        {
            f0 *= colorAttenuation;
            grazingColor = colorAttenuation;
        }
        else
        {
            //this LdotH^4 factor is distinct from the Schlick-like term below.
            result.specularScale = LdotH * LdotH;
            result.specularScale *= result.specularScale;
        }

        const float oneMinusLdotH = 1.0f - LdotH;
        result.negativeNdotLScale = oneMinusLdotH * oneMinusLdotH;
    }

    const float oneMinusLdotH = 1.0f - LdotH;
    const float schlick2 = oneMinusLdotH * oneMinusLdotH;
    const float schlick4 = schlick2 * schlick2;
    const float schlick5 = oneMinusLdotH * schlick4;

    //metallic pixels receive the extra p^4 multiplier found in the DXBC.
    const float fresnelWeight = schlick5 * (1.0f + schlick4 * material.metallicFactor);

    result.value = lerp(f0, grazingColor, fresnelWeight);

    if (material.attenuateSpecularForAlphaMode)
        result.value *= material.metallicFactor;

    return result;
}

//|||||||||||||||||||||||||| LIGHT ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| LIGHT ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| LIGHT ||||||||||||||||||||||||||

LightLobes EvaluateLight(MaterialData material, float3 albedo, float3 normal, float3 halfVector, float3 centerLightDirection, float3 specularLightDirection, float NdotV, float roughnessAlpha, float areaLightEnergyScale)
{
    LightLobes result;

    const float signedNdotL = dot(normal, centerLightDirection);
    const float positiveNdotL = saturate(signedNdotL);
    const float negativeNdotL = saturate(-signedNdotL);
    const float LdotH = saturate(dot(specularLightDirection, halfVector));
    const float NdotH = saturate(dot(normal, halfVector));

    float specularBRDF = EvaluateSpecularBRDF(roughnessAlpha, NdotH, positiveNdotL, NdotV);

    FresnelData fresnel = EvaluateFresnel(material, albedo, LdotH);

    result.specular = specularBRDF * areaLightEnergyScale * fresnel.specularScale * fresnel.value * positiveNdotL;
    result.diffuse = material.diffuseColor * positiveNdotL + material.backscatterColor * (negativeNdotL * fresnel.negativeNdotLScale);

    return result;
}

//|||||||||||||||||||||||||| MICRO SHADOWS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| MICRO SHADOWS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| MICRO SHADOWS ||||||||||||||||||||||||||

float Uncharted4_MicroShadowing(float AO, float NdotL, float opacity)
{
    float aperture = 2.0 * AO * AO;
    float microshadow = saturate(NdotL + aperture - 1.0);
	return lerp(1.0f, microshadow, opacity);
}

//|||||||||||||||||||||||||| CONTACT SHADOWS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| CONTACT SHADOWS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| CONTACT SHADOWS ||||||||||||||||||||||||||

#if defined(ENABLE_CONTACT_SHADOWS)

// Reconstruct the signed view-space Z used by the original shader.
float ContactShadowViewZ(float sampledDepth)
{
    return -1.0f / (sampledDepth * projExtentsZ.z + projExtentsZ.w);
}

// Projects a position from the same view space used by viewPosition, normal,
// lightDir, and g_lightShapeParam in the captured shader.
float2 ViewToScreenUV(float3 vector_viewPosition, ContactShadowProjectionContext projectionContext, float viewDepthSign)
{
    const float viewDepth = vector_viewPosition.z * viewDepthSign;

    if (viewDepth <= CONTACT_SHADOWS_MIN_VIEW_DEPTH)
        return -1.0f.xx;

    const float2 vector_projectionCoordinate = vector_viewPosition.xy / (viewDepth * projExtentsZ.x) - projExtentsXY.xy;

    // Solve H.xy / H.z == vector_projectionCoordinate, where H varies
    // affinely in screen-pixel X/Y. This preserves the changing homogeneous
    // denominator that the previous linear ratio extrapolation discarded.
    const float2 equationDx = projectionContext.homogeneousParam3Dx.xy - vector_projectionCoordinate * projectionContext.homogeneousParam3Dx.z;
    const float2 equationDy = projectionContext.homogeneousParam3Dy.xy - vector_projectionCoordinate * projectionContext.homogeneousParam3Dy.z;
    const float2 equationRhs = vector_projectionCoordinate * projectionContext.homogeneousParam3.z - projectionContext.homogeneousParam3.xy;

    const float determinant = equationDx.x * equationDy.y - equationDy.x * equationDx.y;

    if (abs(determinant) > 1.0e-10f)
    {
        const float2 pixelOffset = float2(
            (equationRhs.x * equationDy.y - equationDy.x * equationRhs.y) / determinant,
            (equationDx.x * equationRhs.y - equationRhs.x * equationDx.y) / determinant);

        const float sampleReciprocalW = projectionContext.reciprocalW + pixelOffset.x * projectionContext.reciprocalWDx + pixelOffset.y * projectionContext.reciprocalWDy;
        const float2 homogeneousSampleUV = projectionContext.homogeneousUV + pixelOffset.x * projectionContext.homogeneousUVDx + pixelOffset.y * projectionContext.homogeneousUVDy;

        if (abs(sampleReciprocalW) > 1.0e-10f)
            return homogeneousSampleUV / sampleReciprocalW;
    }

    return projectionContext.receiverUV + (vector_projectionCoordinate - projectionContext.receiverProjectionCoordinate) * CONTACT_SHADOWS_NDC_TO_UV_SCALE;
}

// rconstructs the depth sample into that same view coordinate system.
float3 ReconstructViewPosition(float2 vector_sampleUV, float sampledDepth, ContactShadowProjectionContext projectionContext)
{
    float3 vector_viewPosition;
    vector_viewPosition.z = ContactShadowViewZ(sampledDepth);

    float2 vector_projectionCoordinate;

    const float2 uvEquationDx = projectionContext.homogeneousUVDx - vector_sampleUV * projectionContext.reciprocalWDx;
    const float2 uvEquationDy = projectionContext.homogeneousUVDy - vector_sampleUV * projectionContext.reciprocalWDy;
    const float2 uvEquationRhs = vector_sampleUV * projectionContext.reciprocalW - projectionContext.homogeneousUV;
    const float uvDeterminant = uvEquationDx.x * uvEquationDy.y - uvEquationDy.x * uvEquationDx.y;

    if (abs(uvDeterminant) > 1.0e-12f)
    {
        const float2 pixelOffset = float2((uvEquationRhs.x * uvEquationDy.y - uvEquationDy.x * uvEquationRhs.y) / uvDeterminant, (uvEquationDx.x * uvEquationRhs.y - uvEquationRhs.x * uvEquationDx.y) / uvDeterminant);
        const float3 homogeneousSample = projectionContext.homogeneousParam3 + pixelOffset.x * projectionContext.homogeneousParam3Dx + pixelOffset.y * projectionContext.homogeneousParam3Dy;

        if (abs(homogeneousSample.z) > 1.0e-10f)
            vector_projectionCoordinate = homogeneousSample.xy / homogeneousSample.z;
        else
            vector_projectionCoordinate = projectionContext.receiverProjectionCoordinate + (vector_sampleUV - projectionContext.receiverUV) / CONTACT_SHADOWS_NDC_TO_UV_SCALE;
    }
    else
    {
        vector_projectionCoordinate = projectionContext.receiverProjectionCoordinate + (vector_sampleUV - projectionContext.receiverUV) / CONTACT_SHADOWS_NDC_TO_UV_SCALE;
    }

    vector_viewPosition.xy = abs(vector_viewPosition.z) * projExtentsZ.x * (vector_projectionCoordinate + projExtentsXY.xy);

    return vector_viewPosition;
}

// Eye depth is perspective-correct along a line that is stepped uniformly in
// screen UV. Since clip W is proportional to positive eye depth for this
// perspective pass, interpolating reciprocal eye depth gives the same result
// as Rebirth's depth-over-W interpolation without requiring a forward matrix.
float PerspectiveCorrectContactShadowDepth(float screenT, float startDepth, float endDepth)
{
    const float inverseStartDepth = rcp(max(startDepth, CONTACT_SHADOWS_MIN_VIEW_DEPTH));
    const float inverseEndDepth = rcp(max(endDepth, CONTACT_SHADOWS_MIN_VIEW_DEPTH));
    return rcp(lerp(inverseStartDepth, inverseEndDepth, saturate(screenT)));
}

void ClipContactShadowScreenRay(float startDistance, float endDistance, inout float exitT)
{
    if (endDistance < 0.0f)
    {
        const float denominator = startDistance - endDistance;

        if (denominator > 1.0e-6f)
            exitT = min(exitT, startDistance / denominator);
    }
}

#if defined(CONTACT_SHADOWS_IMPROVED_THICKNESS)

float CalculateAdaptiveContactShadowThickness(float2 vector_sampleUV, float sampledDepth, float3 vector_sampleViewPosition, float2 vector_inverseDepthDimensions, float contactShadowBias, ContactShadowProjectionContext projectionContext)
{
    // Reconstruct adjacent texel centers at the same scene depth. This uses
    // the custom engine's corrected projection mapping rather than assuming a
    // UE projection matrix layout.
    const float3 vector_sampleViewPositionX = ReconstructViewPosition(vector_sampleUV + float2(vector_inverseDepthDimensions.x, 0.0f), sampledDepth, projectionContext);
    const float3 vector_sampleViewPositionY = ReconstructViewPosition(vector_sampleUV + float2(0.0f, vector_inverseDepthDimensions.y), sampledDepth, projectionContext);

    const float pixelFootprint = max(length(vector_sampleViewPositionX - vector_sampleViewPosition), length(vector_sampleViewPositionY - vector_sampleViewPosition));

    const float minimumThickness = max(CONTACT_SHADOWS_MIN_THICKNESS, contactShadowBias + 1.0e-6f);
    const float maximumThickness = max(CONTACT_SHADOWS_THICKNESS, minimumThickness);

    return clamp(CONTACT_SHADOWS_MIN_THICKNESS + pixelFootprint * CONTACT_SHADOWS_PIXEL_THICKNESS_SCALE, minimumThickness, maximumThickness);
}

#endif

float ContactShadowViewSpace(float3 vector_viewPosition, float3 vector_viewNormal, float3 vector_viewLightDirection, ContactShadowProjectionContext projectionContext, float2 vector_pixelPosition, float depth)
{
    uint depthWidth;
    uint depthHeight;
    depthSampler.GetDimensions(depthWidth, depthHeight);

    const float2 vector_depthDimensions = max(float2(depthWidth, depthHeight), 1.0f.xx);
    const float2 vector_inverseDepthDimensions = rcp(vector_depthDimensions);
    const float resolutionBiasScale = CONTACT_SHADOWS_BIAS_REFERENCE_HEIGHT * max(vector_inverseDepthDimensions.x, vector_inverseDepthDimensions.y);
    const float3 vector_normalizedViewNormal = normalize(vector_viewNormal);
    const float3 rayDirection = normalize(vector_viewLightDirection);
    const float inverseSamples = rcp((float)CONTACT_SHADOWS_SAMPLES);
    const float viewDepthSign = vector_viewPosition.z < 0.0f ? -1.0f : 1.0f;
    const float contactShadowBias = CONTACT_SHADOWS_BIAS * resolutionBiasScale;
    const float contactShadowNormalBias = CONTACT_SHADOWS_NORMAL_BIAS * resolutionBiasScale;

    #if defined(CONTACT_SHADOWS_INTERLEAVED_GRADIENT_NOISE)
    #if USE_FAST_NOISE
    float sampleJitter = noiseTexture.Load(int4((uint2)vector_pixelPosition.xy % 128, LumaSettings.FrameIndex % 32, 0)).x;
    sampleJitter = lerp(0.5f, sampleJitter, saturate(CONTACT_SHADOWS_NOISE_STRENGTH));
    #else
    float sampleJitter = InterleavedGradientNoise(vector_pixelPosition, LumaSettings.FrameIndex);
    sampleJitter = lerp(0.5f, sampleJitter, saturate(CONTACT_SHADOWS_NOISE_STRENGTH));
    #endif
    #else
        float sampleJitter = 0.5f;
    #endif

    // Both offsets are in the same verified FFXV view space as the receiver.
    float3 rayOrigin = vector_viewPosition
        + vector_normalizedViewNormal * contactShadowNormalBias
        + rayDirection * contactShadowBias;

    rayOrigin += rayDirection * depth * 0.05;

    float3 rayEnd = rayOrigin + rayDirection * CONTACT_SHADOWS_RAY_LENGTH;

    const float rayOriginDepth = rayOrigin.z * viewDepthSign;

    if (rayOriginDepth <= CONTACT_SHADOWS_MIN_VIEW_DEPTH)
        return 1.0f;

    // Clip the 3D ray at the camera plane before projection. This is the view-
    // space equivalent of Rebirth's homogeneous clip-W plane test.
    float rayEndDepth = rayEnd.z * viewDepthSign;

    if (rayEndDepth <= CONTACT_SHADOWS_MIN_VIEW_DEPTH)
    {
        const float depthDenominator = rayOriginDepth - rayEndDepth;

        if (depthDenominator <= 1.0e-8f)
            return 1.0f;

        const float cameraPlaneT = saturate(
            (rayOriginDepth - CONTACT_SHADOWS_MIN_VIEW_DEPTH) / depthDenominator);

        rayEnd = lerp(rayOrigin, rayEnd, cameraPlaneT);
        rayEndDepth = rayEnd.z * viewDepthSign;
    }

    float2 vector_rayOriginUV = ViewToScreenUV(rayOrigin, projectionContext, viewDepthSign);
    float2 vector_rayEndUV = ViewToScreenUV(rayEnd, projectionContext, viewDepthSign);

    if (any(vector_rayOriginUV < 0.0f) || any(vector_rayOriginUV > 1.0f))
        return 1.0f;

    // Clip the projected line to the depth texture. Rebirth performs this in
    // homogeneous clip space; after perspective division the ray remains a
    // straight screen-space segment, so the equivalent UV-plane clip is exact.
    float rayExitScreenT = 1.0f;
    ClipContactShadowScreenRay(vector_rayOriginUV.x, vector_rayEndUV.x, rayExitScreenT);
    ClipContactShadowScreenRay(1.0f - vector_rayOriginUV.x, 1.0f - vector_rayEndUV.x, rayExitScreenT);
    ClipContactShadowScreenRay(vector_rayOriginUV.y, vector_rayEndUV.y, rayExitScreenT);
    ClipContactShadowScreenRay(1.0f - vector_rayOriginUV.y, 1.0f - vector_rayEndUV.y, rayExitScreenT);

    if (rayExitScreenT <= 1.0e-4f)
        return 1.0f;

    if (rayExitScreenT < 1.0f)
    {
        vector_rayEndUV = lerp(vector_rayOriginUV, vector_rayEndUV, rayExitScreenT);
        rayEndDepth = PerspectiveCorrectContactShadowDepth(
            rayExitScreenT,
            rayOriginDepth,
            rayEndDepth);
    }

    const float2 vector_rayPixelDelta =
        (vector_rayEndUV - vector_rayOriginUV) * vector_depthDimensions;

    if (dot(vector_rayPixelDelta, vector_rayPixelDelta)
        < CONTACT_SHADOWS_MIN_SCREEN_RAY_LENGTH_PIXELS * CONTACT_SHADOWS_MIN_SCREEN_RAY_LENGTH_PIXELS)
    {
        return 1.0f;
    }

    const float2 vector_uvStep =
        (vector_rayEndUV - vector_rayOriginUV) * inverseSamples;

    float2 vector_sampleUV = mad(vector_uvStep, sampleJitter, vector_rayOriginUV);

    const float receiverNoL = saturate(dot(vector_normalizedViewNormal, rayDirection));
    const float grazingFactor = 1.0f - receiverNoL;
    const float receiverSkipSteps = CONTACT_SHADOWS_SELF_OCCLUSION_SKIP_STEPS + grazingFactor * CONTACT_SHADOWS_GRAZING_EXTRA_SKIP_STEPS;
    const float minimumTraceT = saturate(receiverSkipSteps * inverseSamples);

    float segmentDepth0 = PerspectiveCorrectContactShadowDepth(
        minimumTraceT,
        rayOriginDepth,
        rayEndDepth);

    float occlusion = 1.0f;

    [loop]
    for (int i = 0; i < CONTACT_SHADOWS_SAMPLES; ++i)
    {
        if (any(vector_sampleUV < 0.0f) || any(vector_sampleUV > 1.0f))
            break;

        const float sampleT = ((float)i + sampleJitter) * inverseSamples;
        const float halfStepT = 0.5f * inverseSamples;
        const float segmentEndT = (i == CONTACT_SHADOWS_SAMPLES - 1)
            ? 1.0f
            : saturate(sampleT + halfStepT);

        if (segmentEndT <= minimumTraceT)
        {
            vector_sampleUV += vector_uvStep;
            continue;
        }

        const float segmentDepth1 = PerspectiveCorrectContactShadowDepth(
            segmentEndT,
            rayOriginDepth,
            rayEndDepth);

        // Do not compare against the texel containing the ray's receiver.
        const float2 vector_sampleOffsetPixels =
            (vector_sampleUV - vector_rayOriginUV) * vector_depthDimensions;

        if (dot(vector_sampleOffsetPixels, vector_sampleOffsetPixels) >= 0.25f)
        {
            const float sampledDepth = depthSampler.SampleLevel(
                pointClampSampler,
                vector_sampleUV,
                0.0f);

            const float3 vector_sampleViewPosition = ReconstructViewPosition(
                vector_sampleUV,
                sampledDepth,
                projectionContext);

            // Compare positive camera-axis eye depth, not radial distance.
            const float sceneDepth = vector_sampleViewPosition.z * viewDepthSign;

            #if defined(CONTACT_SHADOWS_IMPROVED_THICKNESS)
            const float rayDepthMin = min(segmentDepth0, segmentDepth1);
            const float rayDepthMax = max(segmentDepth0, segmentDepth1);

            const float projectedThickness = CalculateAdaptiveContactShadowThickness(
                vector_sampleUV,
                sampledDepth,
                vector_sampleViewPosition,
                vector_inverseDepthDimensions,
                contactShadowBias,
                projectionContext);

                const float sceneDepthFront = sceneDepth + contactShadowBias;
                const float sceneDepthBack = sceneDepth + projectedThickness;
                const bool intersectsDepthInterval =
                    rayDepthMax > sceneDepthFront &&
                    rayDepthMin < sceneDepthBack;
            #else
                const float rayDepth = PerspectiveCorrectContactShadowDepth(
                    sampleT,
                    rayOriginDepth,
                    rayEndDepth);
                const float depthDiff = rayDepth - sceneDepth;
                const float projectedThickness =
                    CONTACT_SHADOWS_THICKNESS * CONTACT_SHADOWS_THICKNESS_SCALE;
                const bool intersectsDepthInterval =
                    depthDiff > contactShadowBias &&
                    depthDiff < projectedThickness;
            #endif

            if (intersectsDepthInterval)
            {
                #if defined(CONTACT_SHADOWS_FALLOFF)
                    const float sampleShadow = sampleT * sampleT;
                    occlusion = min(occlusion, sampleShadow);
                #else
                    return 0.0f;
                #endif
            }
        }

        segmentDepth0 = segmentDepth1;
        vector_sampleUV += vector_uvStep;
    }

    #if defined(CONTACT_SHADOWS_FALLOFF)
        occlusion = pow(occlusion, CONTACT_SHADOWS_FALLOFF_CONTRAST);
    #endif

    return occlusion;
}

#endif // ENABLE_CONTACT_SHADOWS

//|||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| SPACE CONVERSIONS ||||||||||||||||||||||||||

float3 ViewDirectionToWorldDirection(float3 vector_viewSpaceDirection)
{
    return normalize(mul(vector_viewSpaceDirection, transpose((float3x3)viewMatrix)));
}

/*
float3 CameraPositionFromViewMatrix()
{
    float3 rotationRows[3] = {
        viewMatrix[0].xyz,
        viewMatrix[1].xyz,
        viewMatrix[2].xyz
    };

    float3 translation = viewMatrix[3].xyz;

    return -float3(
        dot(translation, rotationRows[0]),
        dot(translation, rotationRows[1]),
        dot(translation, rotationRows[2]));
}

float3 CameraPositionFromViewMatrix()
{
    float3x3 viewRotation = (float3x3)viewMatrix;
    return -mul(viewMatrix[3].xyz, transpose(viewRotation));
}
*/

float3 ViewToWorldPosition(float3 viewPosition)
{
    float3x3 viewRotation = (float3x3)viewMatrix;
    float3 untranslatedViewPosition =
        viewPosition - viewMatrix[3].xyz;

    return mul(
        untranslatedViewPosition,
        transpose(viewRotation));
}

float3 CameraPositionFromViewMatrix()
{
    float3 rotationRows[3] = {
        viewMatrix[0].xyz,
        viewMatrix[1].xyz,
        viewMatrix[2].xyz
    };

    float3 translation = viewMatrix[3].xyz;

    return -float3(
        dot(translation, rotationRows[0]),
        dot(translation, rotationRows[1]),
        dot(translation, rotationRows[2]));
}

//|||||||||||||||||||||||||| MAIN ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| MAIN ||||||||||||||||||||||||||
//|||||||||||||||||||||||||| MAIN ||||||||||||||||||||||||||

OutputStruct main(in InputStruct IN)
{
    OutputStruct OUT = (OutputStruct)0;

    //x = black to red (left to right)
    //y = black to green (top to bottom)
    float2 vector_screenUV = IN.param1;

    float sceneBufferDepth = depthSampler.SampleLevel(pointClampSampler, vector_screenUV, 0.0f); //(NOT RAW DEVICE DEPTH, LINEAR EYE DEPTH)
    float4 sceneBufferNormal = normalSampler.SampleLevel(pointClampSampler, vector_screenUV, 0.0f);
    float4 sceneBufferAlbedo = albedoSampler.SampleLevel(pointClampSampler, vector_screenUV, 0.0f);
    float4 sceneBufferSpecular = specularSampler.SampleLevel(pointClampSampler, vector_screenUV, 0.0f);

    //world space normal (x = left | y = up | z = front)
    float3 vector_worldNormalDirection = DecodePackedNormal(sceneBufferNormal);
    float3 vector_viewNormalDirection = vector_worldNormalDirection.x * viewMatrix[0].xyz + vector_worldNormalDirection.y * viewMatrix[1].xyz + vector_worldNormalDirection.z * viewMatrix[2].xyz;

    float3 vector_viewSpaceLightDirection = -lightDir.xyz;
    float3 vector_worldSpaceLightDirection = ViewDirectionToWorldDirection(vector_viewSpaceLightDirection);

    float3 vector_viewPosition;
    vector_viewPosition.z = -1.0f / (sceneBufferDepth * projExtentsZ.z + projExtentsZ.w);
    vector_viewPosition.xy = abs(vector_viewPosition.z) * projExtentsZ.x * ((IN.param3.xy / IN.param3.z) + projExtentsXY.xy);

    float3 vector_worldPosition = ViewToWorldPosition(vector_viewPosition);
    float3 vector_worldRayDirection = normalize(vector_worldPosition - CameraPositionFromViewMatrix());
    float3 vector_worldViewDirection = -vector_worldRayDirection;

    float3 vector_viewRayDirection = normalize(IN.param2.xyz);
    float3 vector_viewViewDirection = -vector_viewRayDirection;
    float NdotV = dot(vector_viewNormalDirection, vector_viewViewDirection);

    //debug
    //sceneBufferAlbedo = float4(1, 1, 1, 1);

    MaterialData materialData = DecodeMaterial(sceneBufferAlbedo, sceneBufferSpecular, sceneBufferNormal);

    #if defined(ENABLE_CONTACT_SHADOWS)
        // SV_Position.w is reciprocal clip W in a pixel shader. Restoring that
        // factor before differentiating makes the extrapolated param3 exactly
        // affine across the primitive, including its changing Z denominator.
        float reciprocalW = IN.Position.w;
        float3 homogeneousParam3 = IN.param3 * reciprocalW;
        float2 homogeneousUV = vector_screenUV * reciprocalW;

        ContactShadowProjectionContext contactShadowProjection;
        contactShadowProjection.receiverUV = vector_screenUV;
        contactShadowProjection.receiverProjectionCoordinate = IN.param3.xy / IN.param3.z;
        contactShadowProjection.homogeneousParam3 = homogeneousParam3;
        contactShadowProjection.homogeneousParam3Dx = ddx(homogeneousParam3);
        contactShadowProjection.homogeneousParam3Dy = ddy(homogeneousParam3);
        contactShadowProjection.reciprocalW = reciprocalW;
        contactShadowProjection.reciprocalWDx = ddx(reciprocalW);
        contactShadowProjection.reciprocalWDy = ddy(reciprocalW);
        contactShadowProjection.homogeneousUV = homogeneousUV;
        contactShadowProjection.homogeneousUVDx = ddx(homogeneousUV);
        contactShadowProjection.homogeneousUVDy = ddy(homogeneousUV);
    #endif

    float shadowTexel = shadowSampler.SampleLevel(pointClampSampler, vector_screenUV, 0.0f);
    float shadow = 1.0f + lightColor.a * (shadowTexel - 1.0f);
    float exposure = ExposureBuffer[0].exposure;

    float3 specularLighting = 0.0f.xxx;
    float3 diffuseLighting = 0.0f.xxx;
    float directionalContactShadow = 1.0f;

    // Directional light ------------------------------------------------------
    {
        float3 halfVector = normalize(vector_viewViewDirection + vector_viewSpaceLightDirection);
        float roughness = max(materialData.perceptualRoughness, MIN_ROUGHNESS);
        float roughnessAlpha = roughness * roughness;

        LightLobes lobes = EvaluateLight(
            materialData,
            sceneBufferAlbedo.rgb,
            vector_viewNormalDirection,
            halfVector,
            vector_viewSpaceLightDirection,
            vector_viewSpaceLightDirection,
            NdotV,
            roughnessAlpha,
            1.0f);

        #if defined(ENABLE_CONTACT_SHADOWS)
            // Existing shadow-map occlusion is already definitive, so avoid a
            // depth march when this pixel receives no directional light.
            if (shadow > 0.0f)
            {
                directionalContactShadow = ContactShadowViewSpace(
                    vector_viewPosition,
                    vector_viewNormalDirection,
                    vector_viewSpaceLightDirection,
                    contactShadowProjection,
                    IN.Position.xy,
                    sceneBufferDepth);
            }
        #endif

		#if defined(ENABLE_MICRO_SHADOWS)
			float microNdotL = saturate(dot(vector_viewNormalDirection, vector_viewSpaceLightDirection));
			float unchartedMicroShadow = Uncharted4_MicroShadowing(sceneBufferAlbedo.a, microNdotL, MICRO_SHADOWS_STRENGTH);
		
			lobes.specular *= unchartedMicroShadow;
			lobes.diffuse *= unchartedMicroShadow;
		#endif

        float directionalVisibility = shadow * lerp(1.0f, directionalContactShadow, saturate(CONTACT_SHADOWS_STRENGTH));
        float3 radiance = exposure * directionalVisibility * lightColor.rgb * (sceneBufferAlbedo.a * PI_RCP);

        specularLighting += lobes.specular * radiance;
        diffuseLighting += lobes.diffuse * radiance;
    }

	//1 = contact-shadow mask
	//OUT.Target0 = float4(directionalContactShadow.xxx, 0.0f);
	//OUT.Target1 = 0.0f.xxxx;
	//return OUT;

    float3 pointToShapeOrigin = vector_viewPosition - g_lightShapeParam.vpos;

    if (dot(pointToShapeOrigin, pointToShapeOrigin) < g_lightShapeParam.range * g_lightShapeParam.range)
    {
        float3 toLightCenter = g_lightShapeParam.vpos - vector_viewPosition;
        float distanceSquared = dot(toLightCenter, toLightCenter);
        float inverseDistance = rsqrt(distanceSquared);
        float3 centerLightDirection = toLightCenter * inverseDistance;

        // Select a direction toward the finite spherical emitter around the
        // perfect reflection ray. This is the capture's area-light correction.
        float3 reflectionDirection = vector_viewRayDirection + 2.0f * NdotV * vector_viewNormalDirection;
        float3 perpendicular = reflectionDirection * dot(toLightCenter, reflectionDirection) - toLightCenter;

        perpendicular = perpendicular * saturate(g_lightParam.specRadius * rsqrt(dot(perpendicular, perpendicular))) + toLightCenter;

        float inverseSpecularVectorLength = rsqrt(dot(perpendicular, perpendicular));
        float3 specularLightDirection = perpendicular * inverseSpecularVectorLength;
        float3 halfVector = normalize(specularLightDirection - vector_viewRayDirection);

        float falloffCoordinate = distanceSquared * abs(g_lightParam.invRangeSquaredAndSmoothFalloff);
        
		if (g_lightParam.invRangeSquaredAndSmoothFalloff <= 0.0f)
            falloffCoordinate = 0.0f;

        float smoothFalloff = max(1.0f - falloffCoordinate * falloffCoordinate, 0.0f);
        smoothFalloff *= smoothFalloff;

        float attenuation = smoothFalloff / (g_lightParam.radius * g_lightParam.radius + distanceSquared);
        float3 pointRadiance = exposure * attenuation * g_lightParam.color;

        float pointRoughness = materialData.perceptualRoughness + g_lightParam.roughnessModifier * (1.0f - materialData.perceptualRoughness);
        pointRoughness = max(pointRoughness, MIN_ROUGHNESS);
        float pointRoughnessAlpha = pointRoughness * pointRoughness;
        float angularRadius = g_lightParam.specRadius * inverseDistance;
        float broadenedAlpha = saturate(pointRoughnessAlpha + angularRadius * (1.0f / 3.0f));
        
		float areaLightEnergyScale = pointRoughnessAlpha / broadenedAlpha;
        areaLightEnergyScale *= areaLightEnergyScale;
        areaLightEnergyScale *= saturate(dot(specularLightDirection, halfVector));

        LightLobes lobes = EvaluateLight(
            materialData,
            sceneBufferAlbedo.rgb,
            vector_viewNormalDirection,
            halfVector,
            centerLightDirection,
            specularLightDirection,
            NdotV,
            pointRoughnessAlpha,
            areaLightEnergyScale);

        float3 radiance = pointRadiance * (sceneBufferAlbedo.a * PI_RCP);

        specularLighting += lobes.specular * radiance;
        diffuseLighting += lobes.diffuse * radiance;
    }

    float3 clampedSpecular = min(specularLighting, 100.0f.xxx);
    float3 clampedDiffuse = min(diffuseLighting, 100.0f.xxx);
    float3 clampedCombined = min(specularLighting + diffuseLighting, 100.0f.xxx);

    OUT.Target0 = float4(materialData.writeSpecularSeparately ? clampedDiffuse : clampedCombined, 0.0f);
    OUT.Target1 = float4(materialData.writeSpecularSeparately ? clampedSpecular : 0.0f.xxx, 0.0f);

    //OUT.Target0 = float4(IN.param2.xyz, 0);
    //OUT.Target0 = float4(vector_worldNormalDirection, 0);
    //OUT.Target0 = float4(vector_viewNormalDirection, 0);
    //OUT.Target0 = float4(vector_viewRayDirection, 0);
    //OUT.Target0 = float4(vector_screenUV, 0, 1);
    //OUT.Target0 = float4(vector_viewSpaceLightDirection.xyz, 1);
    //OUT.Target0 = float4(vector_worldSpaceLightDirection.xyz, 1);
    //OUT.Target0 = float4(saturate(dot(vector_worldNormalDirection, vector_worldSpaceLightDirection)).xxx, 1);
    //OUT.Target0 = float4(saturate(dot(vector_viewNormalDirection, vector_viewSpaceLightDirection)).xxx, 1);
    //OUT.Target0 = float4(vector_worldPosition.xyz, 1);
    //OUT.Target0 = float4(vector_viewDirection.xyz, 1);

    //OUT.Target0 = float4(NdotV.xxx, 1);
    //OUT.Target0 = float4(saturate(dot(vector_worldNormalDirection, vector_worldViewDirection)).xxx, 1);

    return OUT;
}
