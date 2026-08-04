//simulates micro-level shadowing on materials (using material ao) super cheap and performant! (from uncharted 4)
//but this can lead to some materials/objects looking much darker than usual.
//if this is not desired you can just disable to revert to (mostly) original shading
#define ENABLE_MICRO_SHADOWS

#define MICRO_SHADOWS_STRENGTH 1.0

// |||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||
// |||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||
// |||||||||||||||||||||||||| CONFIGURATION - CONTACT SHADOWS ||||||||||||||||||||||||||
// Simple view-space contact shadows for the directional light. Set this to 0
// to compile the reconstructed shader without any contact-shadow code.
#define ENABLE_CONTACT_SHADOWS

// Ray-march quality. Larger values reduce gaps/noise at a proportional cost.
#define CONTACT_SHADOWS_SAMPLES 64

// View-space length of the ray toward the directional light.
#define CONTACT_SHADOWS_RAY_LENGTH 1.0f

// Stratifies one depth lookup inside each ray-march interval. The captured
// buffers expose no reliable frame index, so this is stable spatial noise.
// Set the strength to zero for deterministic interval midpoints.
#define CONTACT_SHADOWS_INTERLEAVED_GRADIENT_NOISE
#define CONTACT_SHADOWS_NOISE_STRENGTH 1.0f

// Maximum camera-depth difference accepted as an occluder.
#define CONTACT_SHADOWS_THICKNESS 0.03f

// Uses a finite ray-depth segment and grows thickness from a small minimum by
// the sampled pixel's view-space footprint. CONTACT_SHADOWS_THICKNESS times
// CONTACT_SHADOWS_THICKNESS_SCALE remains the hard maximum.
//#define CONTACT_SHADOWS_IMPROVED_THICKNESS
#define CONTACT_SHADOWS_MIN_THICKNESS 0.005f
#define CONTACT_SHADOWS_PIXEL_THICKNESS_SCALE 5.0f

// Ignore the receiver-adjacent part of the ray. Grazing rays receive a larger
// exclusion because they remain close to the launching surface for longer.
#define CONTACT_SHADOWS_SELF_OCCLUSION_SKIP_STEPS 0.5f
#define CONTACT_SHADOWS_GRAZING_EXTRA_SKIP_STEPS 1.0f

// Screen-space rays shorter than this cannot be represented reliably by the
// depth buffer and commonly cause uniform darkening on distant geometry.
#define CONTACT_SHADOWS_MIN_SCREEN_RAY_LENGTH_PIXELS 1.75f

// The depth-comparison bias and view-space normal offset.
#define CONTACT_SHADOWS_BIAS 0.0001f
#define CONTACT_SHADOWS_NORMAL_BIAS 0.0001f

// Keeps the configured biases equivalent at 1080p, larger at lower
// resolutions, and smaller at higher resolutions.
#define CONTACT_SHADOWS_BIAS_REFERENCE_HEIGHT 1890.0f

// Overall strength applied to the directional-light visibility.
#define CONTACT_SHADOWS_STRENGTH 1.0f

// Matches the reference implementation's CONTACT_SHADOWS_THICKNESS * 100.
#define CONTACT_SHADOWS_THICKNESS_SCALE 10.0f

// Gradually weakens occlusion found farther along the light ray.
//#define CONTACT_SHADOWS_FALLOFF
#define CONTACT_SHADOWS_FALLOFF_CONTRAST 3.0f

// Converts the reconstructed projection coordinate into texture UV movement.
// This is retained as a fallback for a degenerate interpolant/UV derivative
// basis. Normal contact-shadow projection uses the full homogeneous param3.
#define CONTACT_SHADOWS_NDC_TO_UV_SCALE float2(0.5f, 0.5f)

// Prevents projection through the camera plane.
#define CONTACT_SHADOWS_MIN_VIEW_DEPTH 0.001f

#define CONTACT_SHADOWS_DEBUG_DEPTH_RANGE 1000.0f

static const float PI_RCP                  = 0.318310f;
static const float VISIBILITY_SCALE        = 0.398942f;
static const float MIN_ROUGHNESS           = 0.050000f;
static const float SPECULAR_ALPHA_SCALE    = 0.160630f;
static const float SPECULAR_ALPHA_MID_BIAS = 0.080630f;

SamplerState pointClampSampler : register(s0);

// The DXBC declares a 12-byte structured element and loads the float at byte
// offset 4 from element zero. The original names of the other fields are lost.
struct ExposureValues
{
    float unknown0;
    float exposure;
    float unknown2;
};

StructuredBuffer<ExposureValues> ExposureBuffer : register(t0);
Texture2D<float4> albedoSampler                  : register(t1);
Texture2D<float4> specularSampler                : register(t2);
Texture2D<float4> normalSampler                  : register(t3);
Texture2D<float>  depthSampler                   : register(t4);
Texture2D<float>  shadowSampler                  : register(t5);

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
    // row_major makes matrix[i] address the contiguous c-register shown in the
    // DXBC (c17+i and c22+i respectively).
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

struct MaterialData
{
    float3 diffuseColor;
    float3 specularF0;
    float3 backscatterColor;
    float  metallicFactor;
    float  perceptualRoughness;

    // Inferred behavioral names. Their exact comparisons come from the DXBC.
    bool useSpecialBackscatterFresnel;
    bool attenuateSpecularForAlphaMode;
    bool writeSpecularSeparately;
};

// Decodes the octahedral normal packed across normalTexel.rgb. The unusual
// constants and the small floor-dependent correction are preserved verbatim.
float3 DecodePackedNormal(float4 normalTexel)
{
    float packedSlice = normalTexel.y * 15.937500f;

    float2 oct;
    oct.x = normalTexel.x * 1.992674f + floor(packedSlice) * 0.000488f;
    oct.y = frac(packedSlice) * 2.000489f + normalTexel.z * 0.124542f;
    oct -= 1.0f;

    float3 normal = float3(oct, 1.0f - abs(oct.x) - abs(oct.y));

    if (normal.z < 0.0f)
    {
        normal.xy = (1.0f - abs(oct.yx)) * float2(oct.x >= 0.0f ? 1.0f : -1.0f, oct.y >= 0.0f ? 1.0f : -1.0f);
    }

    return normalize(normal);
}

// The alpha and blue channels both use a triangular 0..1..0 decode, with
// slightly asymmetric constants matching the captured instructions.
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

    // Only the zero-green/zero-blue mode treats specular.r as a metallic-like
    // interpolation factor. In all other modes this value is exactly zero.
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

// This is the captured microfacet denominator/visibility approximation. It is
// intentionally left in the shader's original algebra rather than replaced by
// a named textbook BRDF, since doing that would change the result.
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

struct FresnelData
{
    float3 value;
    float  specularScale;
    float  negativeNdotLScale;
};

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
            // This LdotH^4 factor is distinct from the Schlick-like term below.
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

    // Metallic pixels receive the extra p^4 multiplier found in the DXBC.
    const float fresnelWeight = schlick5 * (1.0f + schlick4 * material.metallicFactor);

    result.value = lerp(f0, grazingColor, fresnelWeight);

    if (material.attenuateSpecularForAlphaMode)
        result.value *= material.metallicFactor;

    return result;
}

struct LightLobes
{
    float3 specular;
    float3 diffuse;
};

LightLobes EvaluateLight(MaterialData material,
                         float3 albedo,
                         float3 normal,
                         float3 halfVector,
                         float3 centerLightDirection,
                         float3 specularLightDirection,
                         float NdotV,
                         float roughnessAlpha,
                         float areaLightEnergyScale)
{
    LightLobes result;

    const float signedNdotL = dot(normal, centerLightDirection);
    const float positiveNdotL = saturate(signedNdotL);
    const float negativeNdotL = saturate(-signedNdotL);
    const float LdotH = saturate(dot(specularLightDirection, halfVector));
    const float NdotH = saturate(dot(normal, halfVector));

    float specularBRDF = EvaluateSpecularBRDF(roughnessAlpha, NdotH, positiveNdotL, NdotV);

    FresnelData fresnel = EvaluateFresnel(material, albedo, LdotH);

    result.specular = specularBRDF
                    * areaLightEnergyScale
                    * fresnel.specularScale
                    * fresnel.value
                    * positiveNdotL;

    result.diffuse = material.diffuseColor * positiveNdotL
                   + material.backscatterColor
                   * (negativeNdotL * fresnel.negativeNdotLScale);

    return result;
}

#if defined(ENABLE_CONTACT_SHADOWS)

float ContactShadowInterleavedGradientNoise(float2 pixelPosition)
{
    const float2 integerPixelPosition = floor(pixelPosition);
    return frac(52.9829189f * frac(dot(integerPixelPosition, float2(0.06711056f, 0.00583715f))));
}

float ContactShadowSampleJitter(float2 pixelPosition)
{
    float jitter = 0.5f;

    #if defined(CONTACT_SHADOWS_INTERLEAVED_GRADIENT_NOISE)
        const float noise = ContactShadowInterleavedGradientNoise(pixelPosition);
        jitter = lerp(0.5f, noise, saturate(CONTACT_SHADOWS_NOISE_STRENGTH));
    #endif

    // Keep samples strictly inside their interval rather than exactly on a
    // boundary shared with the preceding/following interval.
    return clamp(jitter, 1.0e-4f, 1.0f - 1.0e-4f);
}

// Reconstruct the signed view-space Z used by the original shader.
float ContactShadowViewZ(float sampledDepth)
{
    return -1.0f / (sampledDepth * projExtentsZ.z + projExtentsZ.w);
}

// param3 is declared as a perspective-correct interpolant. The rasterizer
// divides it by the same reciprocal-W stored in SV_Position.w. Multiplying the
// two restores the affine homogeneous quantity before taking derivatives.
// Keeping all three components is important: param3.xy / param3.z is a
// projective mapping, not an affine screen coordinate.
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

// Projects a position from the same view space used by viewPosition, normal,
// lightDir, and g_lightShapeParam in the captured shader.
float2 ViewToScreenUV(float3 vector_viewPosition,
                      ContactShadowProjectionContext projectionContext,
                      float viewDepthSign)
{
    const float viewDepth = vector_viewPosition.z * viewDepthSign;

    if (viewDepth <= CONTACT_SHADOWS_MIN_VIEW_DEPTH)
        return -1.0f.xx;

    const float2 vector_projectionCoordinate = vector_viewPosition.xy / (viewDepth * projExtentsZ.x) - projExtentsXY.xy;

    // Solve H.xy / H.z == vector_projectionCoordinate, where H varies
    // affinely in screen-pixel X/Y. This preserves the changing homogeneous
    // denominator that the previous linear ratio extrapolation discarded.
    const float2 equationDx = projectionContext.homogeneousParam3Dx.xy
                            - vector_projectionCoordinate * projectionContext.homogeneousParam3Dx.z;
    const float2 equationDy = projectionContext.homogeneousParam3Dy.xy
                            - vector_projectionCoordinate * projectionContext.homogeneousParam3Dy.z;
    const float2 equationRhs = vector_projectionCoordinate * projectionContext.homogeneousParam3.z
                             - projectionContext.homogeneousParam3.xy;

    const float determinant = equationDx.x * equationDy.y - equationDy.x * equationDx.y;

    if (abs(determinant) > 1.0e-10f)
    {
        const float2 pixelOffset = float2(
            (equationRhs.x * equationDy.y - equationDy.x * equationRhs.y) / determinant,
            (equationDx.x * equationRhs.y - equationRhs.x * equationDx.y) / determinant);

        const float sampleReciprocalW = projectionContext.reciprocalW
                                      + pixelOffset.x * projectionContext.reciprocalWDx
                                      + pixelOffset.y * projectionContext.reciprocalWDy;
        const float2 homogeneousSampleUV = projectionContext.homogeneousUV
                                         + pixelOffset.x * projectionContext.homogeneousUVDx
                                         + pixelOffset.y * projectionContext.homogeneousUVDy;

        if (abs(sampleReciprocalW) > 1.0e-10f)
            return homogeneousSampleUV / sampleReciprocalW;
    }

    return projectionContext.receiverUV
         + (vector_projectionCoordinate
         - projectionContext.receiverProjectionCoordinate)
         * CONTACT_SHADOWS_NDC_TO_UV_SCALE;
}

// Reconstructs the depth sample into that same view coordinate system.
float3 ReconstructViewPosition(
    float2 vector_sampleUV,
    float sampledDepth,
    ContactShadowProjectionContext projectionContext)
{
    float3 vector_viewPosition;
    vector_viewPosition.z = ContactShadowViewZ(sampledDepth);

    float2 vector_projectionCoordinate;
    const float2 uvEquationDx = projectionContext.homogeneousUVDx
                              - vector_sampleUV * projectionContext.reciprocalWDx;
    const float2 uvEquationDy = projectionContext.homogeneousUVDy
                              - vector_sampleUV * projectionContext.reciprocalWDy;
    const float2 uvEquationRhs = vector_sampleUV * projectionContext.reciprocalW
                               - projectionContext.homogeneousUV;
    const float uvDeterminant = uvEquationDx.x * uvEquationDy.y
                              - uvEquationDy.x * uvEquationDx.y;

    if (abs(uvDeterminant) > 1.0e-12f)
    {
        const float2 pixelOffset = float2(
            (uvEquationRhs.x * uvEquationDy.y - uvEquationDy.x * uvEquationRhs.y) / uvDeterminant,
            (uvEquationDx.x * uvEquationRhs.y - uvEquationRhs.x * uvEquationDx.y) / uvDeterminant);

        const float3 homogeneousSample = projectionContext.homogeneousParam3
                                       + pixelOffset.x * projectionContext.homogeneousParam3Dx
                                       + pixelOffset.y * projectionContext.homogeneousParam3Dy;

        if (abs(homogeneousSample.z) > 1.0e-10f)
            vector_projectionCoordinate = homogeneousSample.xy / homogeneousSample.z;
        else
            vector_projectionCoordinate = projectionContext.receiverProjectionCoordinate
                                        + (vector_sampleUV - projectionContext.receiverUV) / CONTACT_SHADOWS_NDC_TO_UV_SCALE;
    }
    else
    {
        vector_projectionCoordinate = projectionContext.receiverProjectionCoordinate
                                    + (vector_sampleUV - projectionContext.receiverUV) / CONTACT_SHADOWS_NDC_TO_UV_SCALE;
    }

    vector_viewPosition.xy = abs(vector_viewPosition.z) * projExtentsZ.x * (vector_projectionCoordinate + projExtentsXY.xy);

    return vector_viewPosition;
}

float ContactShadowSampleProgress(float sampleStep, float inverseSamples)
{
    float sampleProgress = saturate(sampleStep * inverseSamples);

    #if defined(CONTACT_SHADOWS_DISTRIBUTE_SAMPLES_NEAR_RECEIVER)
        sampleProgress *= sampleProgress;
    #endif

    return sampleProgress;
}

#if defined(CONTACT_SHADOWS_IMPROVED_THICKNESS)
float CalculateAdaptiveContactShadowThickness(
    float2 vector_sampleUV,
    float sampledDepth,
    float3 vector_sampleViewPosition,
    float2 vector_inverseDepthDimensions,
    float contactShadowBias,
    ContactShadowProjectionContext projectionContext)
{
    // Reconstruct adjacent texel centers at the same scene depth. This uses
    // the custom engine's corrected projection mapping rather than assuming a
    // UE projection matrix layout.
    const float3 vector_sampleViewPositionX = ReconstructViewPosition(
        vector_sampleUV + float2(vector_inverseDepthDimensions.x, 0.0f),
        sampledDepth,
        projectionContext);
    const float3 vector_sampleViewPositionY = ReconstructViewPosition(
        vector_sampleUV + float2(0.0f, vector_inverseDepthDimensions.y),
        sampledDepth,
        projectionContext);

    const float pixelFootprint = max(
        length(vector_sampleViewPositionX - vector_sampleViewPosition),
        length(vector_sampleViewPositionY - vector_sampleViewPosition));

    const float minimumThickness = max(
        CONTACT_SHADOWS_MIN_THICKNESS,
        contactShadowBias + 1.0e-6f);
    const float maximumThickness = max(
        CONTACT_SHADOWS_THICKNESS * CONTACT_SHADOWS_THICKNESS_SCALE,
        minimumThickness);

    return clamp(
        CONTACT_SHADOWS_MIN_THICKNESS
        + pixelFootprint * CONTACT_SHADOWS_PIXEL_THICKNESS_SCALE,
        minimumThickness,
        maximumThickness);
}
#endif

float ContactShadowViewSpace(
    float3 vector_viewPosition,
    float3 vector_viewNormal,
    float3 vector_viewLightDirection,
    ContactShadowProjectionContext projectionContext,
    float2 vector_pixelPosition)
{
    uint depthWidth;
    uint depthHeight;
    depthSampler.GetDimensions(depthWidth, depthHeight);

    const float2 vector_depthDimensions = max(float2(depthWidth, depthHeight), 1.0f.xx);
    const float2 vector_inverseDepthDimensions = rcp(vector_depthDimensions);
    const float resolutionBiasScale = CONTACT_SHADOWS_BIAS_REFERENCE_HEIGHT
                                    * max(vector_inverseDepthDimensions.x, vector_inverseDepthDimensions.y);

    const float3 vector_normalizedViewNormal = normalize(vector_viewNormal);
    const float3 rayDirection = normalize(vector_viewLightDirection);
    const float inverseSamples = rcp((float)CONTACT_SHADOWS_SAMPLES);
    const float viewDepthSign = vector_viewPosition.z < 0.0f ? -1.0f : 1.0f;
    const float contactShadowBias = CONTACT_SHADOWS_BIAS * resolutionBiasScale;
    const float contactShadowNormalBias = CONTACT_SHADOWS_NORMAL_BIAS * resolutionBiasScale;
    const float sampleJitter = ContactShadowSampleJitter(vector_pixelPosition);

    // Both offsets are in the same view space as the reconstructed receiver.
    float3 rayOrigin = vector_viewPosition
                     + vector_normalizedViewNormal * contactShadowNormalBias
                     + rayDirection * contactShadowBias;
    const float3 rayEnd = rayOrigin + rayDirection * CONTACT_SHADOWS_RAY_LENGTH;

    const float rayOriginDepth = rayOrigin.z * viewDepthSign;

    if (rayOriginDepth <= CONTACT_SHADOWS_MIN_VIEW_DEPTH)
        return 1.0f;

    // If the entire ray projects to less than one pixel, the depth buffer
    // cannot distinguish it from its receiver. Rejecting it prevents the
    // distance-dependent blanket shadowing seen in the earlier version.
    const float rayEndDepth = rayEnd.z * viewDepthSign;

    if (rayEndDepth > CONTACT_SHADOWS_MIN_VIEW_DEPTH)
    {
        const float2 vector_rayOriginUV = ViewToScreenUV(rayOrigin, projectionContext, viewDepthSign);
        const float2 vector_rayEndUV = ViewToScreenUV(rayEnd, projectionContext, viewDepthSign);
        const float2 vector_rayPixelDelta = (vector_rayEndUV - vector_rayOriginUV) * vector_depthDimensions;

        if (dot(vector_rayPixelDelta, vector_rayPixelDelta)
            < CONTACT_SHADOWS_MIN_SCREEN_RAY_LENGTH_PIXELS * CONTACT_SHADOWS_MIN_SCREEN_RAY_LENGTH_PIXELS)
        {
            return 1.0f;
        }
    }

    const float receiverNoL = saturate(dot(vector_normalizedViewNormal, rayDirection));
    const float grazingFactor = 1.0f - receiverNoL;
    const float receiverSkipSteps = CONTACT_SHADOWS_SELF_OCCLUSION_SKIP_STEPS
                                  + grazingFactor * CONTACT_SHADOWS_GRAZING_EXTRA_SKIP_STEPS;

    float occlusion = 1.0f;

    [loop]
    for (int i = 0; i < CONTACT_SHADOWS_SAMPLES; ++i)
    {
        const float intervalStartStep = (float)i;
        const float intervalEndStep = (float)i + 1.0f;

        if (intervalEndStep <= receiverSkipSteps)
            continue;

        // Preserve complete depth coverage with deterministic interval edges,
        // while moving only the texture lookup inside the surviving interval.
        // Re-mapping the jitter after the receiver skip prevents samples from
        // being clamped into a noisy pile at the exclusion boundary.
        const float segmentStartStep = max(intervalStartStep, receiverSkipSteps);
        const float sampleStep = lerp(segmentStartStep, intervalEndStep, sampleJitter);
        const float sampleProgress = ContactShadowSampleProgress(sampleStep, inverseSamples);
        const float segmentStartProgress = ContactShadowSampleProgress(segmentStartStep, inverseSamples);
        const float segmentEndProgress = ContactShadowSampleProgress(intervalEndStep, inverseSamples);
        const float falloffProgress = ContactShadowSampleProgress(
            0.5f * (segmentStartStep + intervalEndStep),
            inverseSamples);

        const float3 vector_samplePosition = rayOrigin + rayDirection * (CONTACT_SHADOWS_RAY_LENGTH * sampleProgress);
        const float rayDepth = vector_samplePosition.z * viewDepthSign;

        if (rayDepth <= CONTACT_SHADOWS_MIN_VIEW_DEPTH)
            break;

        const float2 vector_sampleUV = ViewToScreenUV(vector_samplePosition, projectionContext, viewDepthSign);

        if (vector_sampleUV.x < 0.0f || vector_sampleUV.x > 1.0f || vector_sampleUV.y < 0.0f || vector_sampleUV.y > 1.0f)
            break;

        // Do not compare against the texel containing the ray's receiver.
        // This is separate from normal/light bias and remains robust as the
        // receiver becomes sub-pixel at distance.
        const float2 vector_sampleOffsetPixels =
            (vector_sampleUV - projectionContext.receiverUV) * vector_depthDimensions;

        if (dot(vector_sampleOffsetPixels, vector_sampleOffsetPixels) < 0.25f)
            continue;

        const float sampledDepth = depthSampler.SampleLevel(pointClampSampler, vector_sampleUV, 0.0f);
        const float3 vector_sampleViewPosition = ReconstructViewPosition(vector_sampleUV, sampledDepth, projectionContext);

        // Compare eye depth, not radial camera distance. Radial distance varies
        // with view angle and made thickness/culling camera-angle dependent.
        const float sceneDepth = vector_sampleViewPosition.z * viewDepthSign;
        #if defined(CONTACT_SHADOWS_IMPROVED_THICKNESS)
            const float3 vector_segmentStartPosition = rayOrigin
                + rayDirection * (CONTACT_SHADOWS_RAY_LENGTH * segmentStartProgress);
            const float3 vector_segmentEndPosition = rayOrigin
                + rayDirection * (CONTACT_SHADOWS_RAY_LENGTH * segmentEndProgress);
            const float segmentDepth0 = vector_segmentStartPosition.z * viewDepthSign;
            const float segmentDepth1 = vector_segmentEndPosition.z * viewDepthSign;
            const float rayDepthMin = min(segmentDepth0, segmentDepth1);
            const float rayDepthMax = max(segmentDepth0, segmentDepth1);

            const float projectedThickness = CalculateAdaptiveContactShadowThickness(
                vector_sampleUV,
                sampledDepth,
                vector_sampleViewPosition,
                vector_inverseDepthDimensions,
                contactShadowBias,
                projectionContext);

            const bool intersectsDepthInterval =
                rayDepthMax > sceneDepth + contactShadowBias &&
                rayDepthMin < sceneDepth + projectedThickness;
        #else
            const float depthDiff = rayDepth - sceneDepth;
            const float projectedThickness = CONTACT_SHADOWS_THICKNESS * CONTACT_SHADOWS_THICKNESS_SCALE;
            const bool intersectsDepthInterval =
                depthDiff > contactShadowBias &&
                depthDiff < projectedThickness;
        #endif

        if (intersectsDepthInterval)
        {
            #if defined(CONTACT_SHADOWS_FALLOFF)
                // Keep opacity deterministic; noise changes which scene depth
                // is tested, not the strength assigned to a successful hit.
                const float sampleShadow = falloffProgress * falloffProgress;
                occlusion = min(occlusion, sampleShadow);
            #else
                return 0.0f;
            #endif
        }
    }

    #if defined(CONTACT_SHADOWS_FALLOFF)
        occlusion = pow(occlusion, CONTACT_SHADOWS_FALLOFF_CONTRAST);
    #endif

    return occlusion;
}

#endif // ENABLE_CONTACT_SHADOWS

float Uncharted4_MicroShadowing(float AO, float NdotL, float opacity)
{
    float aperture = 2.0 * AO * AO;
    float microshadow = saturate(NdotL + aperture - 1.0);
	return lerp(1.0f, microshadow, opacity);
}

OutputStruct EditedShaderPS(in InputStruct IN)
{
    OutputStruct OUT = (OutputStruct)0;

    const float depth = depthSampler.SampleLevel(pointClampSampler, IN.param1, 0.0f);
    const float4 normalTexel = normalSampler.SampleLevel(pointClampSampler, IN.param1, 0.0f);
    const float4 albedoTexel = albedoSampler.SampleLevel(pointClampSampler, IN.param1, 0.0f);
    const float4 specularTexel = specularSampler.SampleLevel(pointClampSampler, IN.param1, 0.0f);

    MaterialData material = DecodeMaterial(albedoTexel, specularTexel, normalTexel);

    const float3 decodedNormal = DecodePackedNormal(normalTexel);

    // Written explicitly to preserve the exact three register rows/columns used
    // by the DXBC regardless of the source compiler's matrix-major convention.
    const float3 normal =
        decodedNormal.x * viewMatrix[0].xyz +
        decodedNormal.y * viewMatrix[1].xyz +
        decodedNormal.z * viewMatrix[2].xyz;

    // param2 points from the camera toward the shaded point. The shader uses
    // its negation as V but also retains the forward ray for reflection math.
    const float3 viewRay = normalize(IN.param2.xyz);
    const float NdotV = dot(normal, -viewRay);

    // Preserve the exact depth reconstruction used by the original shader.
    const float depthDenominator = depth * projExtentsZ.z + projExtentsZ.w;

    float3 viewPosition;
    viewPosition.z = -1.0f / depthDenominator;
    const float2 receiverProjectionCoordinate = IN.param3.xy / IN.param3.z;

    viewPosition.xy = abs(viewPosition.z) * projExtentsZ.x * (receiverProjectionCoordinate + projExtentsXY.xy);

    #if defined(ENABLE_CONTACT_SHADOWS)
        // SV_Position.w is reciprocal clip W in a pixel shader. Restoring that
        // factor before differentiating makes the extrapolated param3 exactly
        // affine across the primitive, including its changing Z denominator.
        const float reciprocalW = IN.Position.w;
        const float3 homogeneousParam3 = IN.param3 * reciprocalW;
        const float2 homogeneousUV = IN.param1 * reciprocalW;

        ContactShadowProjectionContext contactShadowProjection;
        contactShadowProjection.receiverUV = IN.param1;
        contactShadowProjection.receiverProjectionCoordinate = receiverProjectionCoordinate;
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

    const float shadowTexel = shadowSampler.SampleLevel(pointClampSampler, IN.param1, 0.0f);
    const float shadow = 1.0f + lightColor.a * (shadowTexel - 1.0f);
    const float exposure = ExposureBuffer[0].exposure;

    float3 specularLighting = 0.0f.xxx;
    float3 diffuseLighting = 0.0f.xxx;
    float directionalContactShadow = 1.0f;

    // Directional light ------------------------------------------------------
    {
        const float3 directionalLightDirection = -lightDir.xyz;
        const float3 halfVector = normalize(-viewRay + directionalLightDirection);
        const float roughness = max(material.perceptualRoughness, MIN_ROUGHNESS);
        const float roughnessAlpha = roughness * roughness;

        LightLobes lobes = EvaluateLight(
            material,
            albedoTexel.rgb,
            normal,
            halfVector,
            directionalLightDirection,
            directionalLightDirection,
            NdotV,
            roughnessAlpha,
            1.0f);

        #if defined(ENABLE_CONTACT_SHADOWS)
            // Existing shadow-map occlusion is already definitive, so avoid a
            // depth march when this pixel receives no directional light.
            //if (shadow > 0.0f)
            {
                // The captured shader already consumes -lightDir in the same
                // view space as the transformed normal and param2. Applying
                // viewMatrix here would double-transform the light direction.
                const float3 directionalLightViewDirection = directionalLightDirection;

                directionalContactShadow = ContactShadowViewSpace(
                    viewPosition,
                    normal,
                    directionalLightViewDirection,
                    contactShadowProjection,
                    IN.Position.xy);
            }
        #endif

		#if defined(ENABLE_MICRO_SHADOWS)
			const float microNdotL = saturate(dot(normal, directionalLightDirection));
			float unchartedMicroShadow = Uncharted4_MicroShadowing(albedoTexel.a, microNdotL, MICRO_SHADOWS_STRENGTH);
		
			lobes.specular *= unchartedMicroShadow;
			lobes.diffuse *= unchartedMicroShadow;
		#endif

        const float directionalVisibility = shadow * lerp(1.0f, directionalContactShadow, saturate(CONTACT_SHADOWS_STRENGTH));
        const float3 radiance = exposure * directionalVisibility * lightColor.rgb * (albedoTexel.a * PI_RCP);

        specularLighting += lobes.specular * radiance;
        diffuseLighting += lobes.diffuse * radiance;
    }

	//1 = contact-shadow mask
	//OUT.Target0 = float4(directionalContactShadow.xxx, 0.0f);
	//OUT.Target1 = 0.0f.xxxx;
	//return OUT;

	//2 = reconstructed eye depth
	//const float debugEyeDepth = saturate(abs(viewPosition.z) / CONTACT_SHADOWS_DEBUG_DEPTH_RANGE);
	//OUT.Target0 = float4(debugEyeDepth.xxx, 0.0f);
	//OUT.Target1 = 0.0f.xxxx;
	//return OUT;

	//3 = raw sampled depth.
	//OUT.Target0 = float4(depth.xxx, 0.0f);
	//OUT.Target1 = 0.0f.xxxx;
	//return OUT;

    const float3 pointToShapeOrigin = viewPosition - g_lightShapeParam.vpos;

    if (dot(pointToShapeOrigin, pointToShapeOrigin) < g_lightShapeParam.range * g_lightShapeParam.range)
    {
        const float3 toLightCenter = g_lightShapeParam.vpos - viewPosition;
        const float distanceSquared = dot(toLightCenter, toLightCenter);
        const float inverseDistance = rsqrt(distanceSquared);
        const float3 centerLightDirection = toLightCenter * inverseDistance;

        // Select a direction toward the finite spherical emitter around the
        // perfect reflection ray. This is the capture's area-light correction.
        const float3 reflectionDirection = viewRay + 2.0f * NdotV * normal;
        float3 perpendicular = reflectionDirection * dot(toLightCenter, reflectionDirection) - toLightCenter;

        perpendicular = perpendicular * saturate(g_lightParam.specRadius * rsqrt(dot(perpendicular, perpendicular))) + toLightCenter;

        const float inverseSpecularVectorLength = rsqrt(dot(perpendicular, perpendicular));
        const float3 specularLightDirection = perpendicular * inverseSpecularVectorLength;
        const float3 halfVector = normalize(specularLightDirection - viewRay);

        float falloffCoordinate = distanceSquared * abs(g_lightParam.invRangeSquaredAndSmoothFalloff);
        
		if (g_lightParam.invRangeSquaredAndSmoothFalloff <= 0.0f)
            falloffCoordinate = 0.0f;

        float smoothFalloff = max(1.0f - falloffCoordinate * falloffCoordinate, 0.0f);
        smoothFalloff *= smoothFalloff;

        const float attenuation = smoothFalloff / (g_lightParam.radius * g_lightParam.radius + distanceSquared);
        const float3 pointRadiance = exposure * attenuation * g_lightParam.color;

        float pointRoughness = material.perceptualRoughness + g_lightParam.roughnessModifier * (1.0f - material.perceptualRoughness);
        pointRoughness = max(pointRoughness, MIN_ROUGHNESS);
        const float pointRoughnessAlpha = pointRoughness * pointRoughness;
        const float angularRadius = g_lightParam.specRadius * inverseDistance;
        const float broadenedAlpha = saturate(pointRoughnessAlpha + angularRadius * (1.0f / 3.0f));
        
		float areaLightEnergyScale = pointRoughnessAlpha / broadenedAlpha;
        areaLightEnergyScale *= areaLightEnergyScale;
        areaLightEnergyScale *= saturate(dot(specularLightDirection, halfVector));

        LightLobes lobes = EvaluateLight(
            material,
            albedoTexel.rgb,
            normal,
            halfVector,
            centerLightDirection,
            specularLightDirection,
            NdotV,
            pointRoughnessAlpha,
            areaLightEnergyScale);

        const float3 radiance = pointRadiance * (albedoTexel.a * PI_RCP);

        specularLighting += lobes.specular * radiance;
        diffuseLighting += lobes.diffuse * radiance;
    }

    const float3 clampedSpecular = min(specularLighting, 100.0f.xxx);
    const float3 clampedDiffuse = min(diffuseLighting, 100.0f.xxx);
    const float3 clampedCombined = min(specularLighting + diffuseLighting, 100.0f.xxx);

    OUT.Target0 = float4(material.writeSpecularSeparately ? clampedDiffuse : clampedCombined, 0.0f);
    OUT.Target1 = float4(material.writeSpecularSeparately ? clampedSpecular : 0.0f.xxx, 0.0f);
    return OUT;
}
