#include "../Includes/Common.hlsl"

cbuffer Global : register(b0)
{
  float4 EnvironmentLuminances : packoffset(c0);
  float4 FakeEarthShadowPlane : packoffset(c1);
  float4 GlobalLightsIntensity : packoffset(c2);
  float4 GlobalWeatherControl : packoffset(c3);
  float4 MaterialWetnessParams[22] : packoffset(c4);
  float4 WindGlobalTurbulence : packoffset(c26);
  float4 WindVelocityTextureCoverage : packoffset(c27);
  float4 WorldLoadingRingSizes[2] : packoffset(c28);

  struct SGlobalDebugValues
  {
    float debugValue0;
    float debugValue1;
    float debugValue2;
    float debugValue3;
  } DebugValues : packoffset(c30);

  float3 SunShadowDirection : packoffset(c31);
  float CrowdAnimationStartTime : packoffset(c31.w);
  float3 WindGlobalNoiseTextureChannelSel : packoffset(c32);
  float GlobalReflectionTextureBlendRatio : packoffset(c32.w);
  float3 WindGlobalNoiseTextureCoverage : packoffset(c33);
  float GlobalWaterLevel : packoffset(c33.w);

  struct SGlobalScalars
  {
    float time;
    float staticReflectionIntensity;
    float gameDeltaTime;
  } GlobalScalars : packoffset(c34);

  float RcpStaticReflectionExposureScale : packoffset(c34.w);
  float2 GlobalNoiseSampler2DSquareSize : packoffset(c35);
  float SandstormIntensity : packoffset(c35.z);
  float StaticReflectionIntensityDest : packoffset(c35.w);
  float2 WindNoiseDeltaVector : packoffset(c36);
  float TimeOfDay : packoffset(c36.z);
  float VertexAOIntensity : packoffset(c36.w);
  float2 WindVector : packoffset(c37);
}

cbuffer Viewport : register(b1)
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

cbuffer HDREffects : register(b2)
{
  float4 AdditionalBlurParams : packoffset(c0);
  float4 BokehParams : packoffset(c1);
  float4 CoCComputation : packoffset(c2);
  float4 DirectionalBlurUVScaleBias : packoffset(c3);
  float4 SourceSize : packoffset(c4);
  float2 BokehContrast : packoffset(c5);
  float CoCTexelBlurRange : packoffset(c5.z);
  float2 CoCRange : packoffset(c6);
}

cbuffer HDRLighting : register(b3)
{
  float4 AdaptationState : packoffset(c0);
  float4 ExpositionParams : packoffset(c1);
  float4 LensParams1 : packoffset(c2);
  float4 LensParams2 : packoffset(c3);
  float4 UIParams : packoffset(c4);
  float4 ViewportResolution : packoffset(c5);
  float3 CameraXAxis : packoffset(c6);
  float ChromaticAberationStrength : packoffset(c6.w);
  float3 CameraYAxis : packoffset(c7);
  float DistoritionKCube : packoffset(c7.w);
  float3 CameraZAxis : packoffset(c8);
  float DistoritionKCylindrical : packoffset(c8.w);
  float3 MPEGArtefactColorProbability : packoffset(c9);
  float FilmGrainIntensity : packoffset(c9.w);
  float2 ProjectionOffset : packoffset(c10);
  float FilmGrainVignetingRelation : packoffset(c10.z);
  float MPEGArtefactArtefactBlockShift : packoffset(c10.w);
  float MPEGArtefactBlocksPerLargeBlock : packoffset(c11);
  float MPEGArtefactBlockyFactor : packoffset(c11.y);
  float MPEGArtefactBlockyFactorBlend : packoffset(c11.z);
  float MPEGArtefactLineProbability : packoffset(c11.w);
  float MPEGArtefactSmallBlockSize : packoffset(c12);
  float MPEGArtefactSquareProbability : packoffset(c12.y);
  float MPEGArtefactTimeScale : packoffset(c12.z);
  float ProjectionScaleInv : packoffset(c12.w);
}

// Implies MSAA
#if _0A7D2AB7 || _EA7FA4E5
#define TEMPORAL_FILTER 1
#endif

#if defined(TEMPORAL_FILTER) || _AD6E5AAF || _67A672D7
#define MS 1
#endif

#if _0A7D2AB7 || _AD6E5AAF || _10FA30C8 || _EA7FA4E5 || _67A672D7 || _C8651827
#define COLOR_GRADING_TONEMAP_LUT 1
#endif

// Specify the ones that don't have bloom here, given it's not the default case
#if _EA7FA4E5 || _67A672D7 || _C8651827
#define BLOOM 0
#endif

#ifndef TEMPORAL_FILTER
#define TEMPORAL_FILTER 0
#endif
#ifndef MS
#define MS 0
#endif
#ifndef COLOR_GRADING_TONEMAP_LUT
#define COLOR_GRADING_TONEMAP_LUT 0
#endif
#ifndef BLOOM
#define BLOOM 1
#endif
#ifndef ENABLE_DITHER
#define ENABLE_DITHER 1
#endif

SamplerState ColorClamp_s : register(s0);
#if MS

  #if COLOR_GRADING_TONEMAP_LUT

    SamplerState ColorClamp2D_s : register(s1);
    SamplerState ColorWrap_s : register(s2);
    SamplerState HDRLighting__LensDirtTexture__SampObj___s : register(s3);

  #else

    SamplerState ColorWrap_s : register(s1);
    SamplerState HDRLighting__LensDirtTexture__SampObj___s : register(s2);

  #endif

#else

  #if COLOR_GRADING_TONEMAP_LUT

    SamplerState ColorClamp2D_s : register(s1);
    SamplerState ColorPointClamp_s : register(s2);
    SamplerState ColorWrap_s : register(s3);
    SamplerState HDRLighting__LensDirtTexture__SampObj___s : register(s4);

  #else

    SamplerState ColorPointClamp_s : register(s1);
    SamplerState ColorWrap_s : register(s2);
    SamplerState HDRLighting__LensDirtTexture__SampObj___s : register(s3);

  #endif

#endif
Texture2D<float4> Global__GlobalNoiseSampler2D__TexObj__ : register(t0);
Texture2D<float4> HDREffects__ColorAndMaskTexture__TexObj__ : register(t1); // DoF

#if MS
Texture2DMS<float> HDREffects__DepthTextureMS : register(t2);
#else
Texture2D<float> HDREffects__DepthTexture : register(t2);
#endif
Texture2D<float4> HDRLighting__BloomMap__TexObj__ : register(t3);
Texture2D<float4> HDRLighting__Frame__TexObj__ : register(t4); // Scene
#if BLOOM
Texture2D<float4> HDRLighting__LensDirtBloomSource__TexObj__ : register(t5);
Texture2D<float4> HDRLighting__LensDirtTexture__TexObj__ : register(t6);
Texture2D<float4> HDRLighting__PostFxMaskTexture__TexObj__ : register(t7);
Texture3D<float4> HDRLighting__ColorRemapHDRTexture : register(t8);
#else
Texture2D<float4> HDRLighting__PostFxMaskTexture__TexObj__ : register(t5);
Texture3D<float4> HDRLighting__ColorRemapHDRTexture : register(t6);
#endif

static float LUTSize = 32.0;
static float LUTMax = LUTSize - 1.0;

float3 EncodeLUTInput(float3 color)
{
  return saturate(log2(abs(color)) * 0.0632478595 + 0.690302372); // LUT Log encoding (Abs+Saturate because it's free) (negative values are probably not supported)
}
float3 DecodeLUTInput(float3 color)
{
  return exp2((color - 0.690302372) / 0.0632478595); // LUT Log decoding (this decodes the LUT input back, not necessarily the LUT output, which might be linear already)
}

// Corrects transfer function encoded LUT coordinates to return more accurate LUT colors for LUTs that have an encoded input but linear output.
// Assuming a 2 pixels 1D LUT, the first point would map 0 to 0 and the second point 1 to 1.
// Now, if the LUT was meant to be sampled in gamma 2.2 space (as it should, otherwise the point distribution would all be focused on highlights),
// and we sampled 0.5 from the LUT, ideally we'd get 0.5 in output too, but if the LUT output was linear space (e.g. UNORM_SRGB, or FLOAT textures),
// we'd then expect the result to be roughly 0.218 (0.5^2.2, similar to mid grey), but instead we'd get 0.5, which isn't correct, as it's much brighter than expected.
// This contributes to creating banding and generally broken greadients (incontiguous steps).
// This formula skewes the input coordinates in a way that forces the output to return 0.218, without any downsides.
// For LUTs that use the same encoding and decoding formula, then the contiguity error from interpolated mid points isn't really problematic, unless the encoding curve suddenly changed between two LUT points (which usually isn't the case), hence a basic blend is pretty accurate already.
// Note that in cases the LUT inverted colors (e.g. mapping 0 to 1 and 1 to 0) this code actually makes the output less accurate, however, it's an acceptable consequence, which almost never arises and can be avoided in other ways (by branching out flipped LUTs).
// For cases where the LUT is also very strong in intensity, this also doesn't help as much as weaker luts, however, it will still skew the output towards more accurate results.
// 
// It can also be used with tetrahedral interpolation.
// This expects input coordinates within the 0-1 range (prior to acknowledging the half texel offset of LUTs, which should be applied after, just before actually sampling the LUT).
float3 AdjustLUTCoordinatesForLinearOutputLUT(const float3 encodedLUTCoordinates)
{
  float3 previousEncodedLUTCoordinates = floor(encodedLUTCoordinates * LUTMax) / LUTMax;
  float3 nextEncodedLUTCoordinates = ceil(encodedLUTCoordinates * LUTMax) / LUTMax;
  float3 previousLinearLUTCoordinates = DecodeLUTInput(previousEncodedLUTCoordinates);
  float3 linearLUTCoordinates = DecodeLUTInput(encodedLUTCoordinates);
  float3 nextLinearLUTCoordinates = DecodeLUTInput(nextEncodedLUTCoordinates);
  // Every step size is different as it depends on where we are within the transfer function range.
  const float3 stepSize = nextLinearLUTCoordinates - previousLinearLUTCoordinates;
  // If "stepSize" is zero (due to the LUT pixel coords being exactly an integer), whether alpha is zero or one won't matter as "previousEncodedLUTCoordinates" and "nextEncodedLUTCoordinates" will be identical.
  const float3 blendAlpha = safeDivision(linearLUTCoordinates - previousLinearLUTCoordinates, stepSize, 1);
  return lerp(previousEncodedLUTCoordinates, nextEncodedLUTCoordinates, blendAlpha);
}

// From Stephen Hill
float3 ACESFittedAP1(float3 color)
{
  // sRGB => XYZ => D65_2_D60 => AP1 => RRT_SAT
  const float3x3 ACESInputMat = {
    { 0.59719, 0.35458, 0.04823 },
    { 0.07600, 0.90834, 0.01566 },
    { 0.02840, 0.13383, 0.83777 }
  };

  // ODT_SAT => XYZ => D60_2_D65 => sRGB
  const float3x3 ACESOutputMat = {
    { 1.60475, -0.53108, -0.07367 },
    { -0.10208, 1.10813, -0.00605 },
    { -0.00327, -0.07276, 1.07602 }
  };

  color = mul(ACESInputMat, color);

  float3 a = color * (color + 0.0245786) - 0.000090537;
  float3 b = color * (0.983729 * color + 0.4329510) + 0.238081;
  color = a / b;

  color = mul(ACESOutputMat, color);

  return color;
}

// TODO: other permutations... some don't even have "HDRLighting__Frame__TexObj__", and also maybe ignore the ones "HDREffects__CoCBuffer__TexObj__", and ignore these "HDRColorGradingLUTs__HDRColorGradingLUTsTextures00__TexObj__" "HDREffects__BlurParamsTexture__TexObj__"
void main(
  linear centroid float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4;
  int4 r1i, r2i;

  int msIndex = 0;
  float uvHorOffset = 0.0;
  int2 pixelCoords = v1.xy;
#if TEMPORAL_FILTER
  int oddPixel = pixelCoords.y & 1;
  msIndex = pixelCoords.y ^ 1;
  uvHorOffset = ViewportSize.z * (float)oddPixel * 0.5; // TODO: correctly decompiled but what is this??? Is this some hor jitter on screen space buffers?
#endif // TEMPORAL_FILTER
  float2 uv = v0.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
  float2 altUV = uv + float2(uvHorOffset, 0.0);

#if MS
  int2 depthPixelCoords = ViewportSize.xy * uv * float2(1, 0.5); // TODO: why is y scaled by 0.5??? Is it because the resolution is 50% on one axis?
  float depth = HDREffects__DepthTextureMS.Load(depthPixelCoords, msIndex).x; // Note: theoretically we should take the average of all depth MS samples? It'd make little difference
#else
  float depth = HDREffects__DepthTexture.SampleLevel(ColorPointClamp_s, uv, 0).x;
#endif // MS
  r0.x = -dot(float2(depth, 1.0), InvProjectionMatrixDepth._m22_m32) / dot(float2(depth, 1.0), InvProjectionMatrixDepth._m23_m33);
  r0.x = r0.x * CoCComputation.x + CoCComputation.y;
  r0.x = CoCComputation.z / r0.x;
  r0.x = CoCComputation.w + r0.x;
  r0.x = CoCTexelBlurRange * r0.x;
  r0.x = max(CoCRange.x, r0.x);
  r0.x = min(CoCRange.y, r0.x);
  r0.x = r0.x * r0.x;
  float dof = saturate(r0.x * BokehParams.z + -0.5);
  
  float3 sceneColor = HDRLighting__Frame__TexObj__.Sample(ColorWrap_s, uv).rgb;
  float4 dofSample = HDREffects__ColorAndMaskTexture__TexObj__.Sample(ColorClamp_s, altUV).xyzw;
  float3 dofColor = dofSample.rgb;
  float dofIntensity = min(1, max(dofSample.w, dof));
#if BLOOM
  float4 lensDirtBloomSample = HDRLighting__LensDirtBloomSource__TexObj__.Sample(ColorClamp_s, uv).xyzw;
  float3 lensDirtBloomColor = lensDirtBloomSample.rgb;
  float3 lensDirtColor = HDRLighting__LensDirtTexture__TexObj__.Sample(HDRLighting__LensDirtTexture__SampObj___s, altUV).rgb * LensParams1.x;
#endif // BLOOM
  // This bloom is applied even if "BLOOM" is off (meaning the user had turned off bloom in the settings)
  float4 bloomSample = HDRLighting__BloomMap__TexObj__.Sample(ColorClamp_s, altUV).xyzw;
  float3 bloomColor = bloomSample.rgb;
  
  float3 postProcessedColor = lerp(sceneColor, dofColor, dofIntensity);
  bloomColor = max(0.0, bloomColor); // Clamp bloom negative colors just in case they had happened, they'd be trash
  postProcessedColor += bloomColor;
#if 1 // Luma: disabled? // TODO!!!
  // Pre tonemap bloom
  postProcessedColor /= bloomSample.w + 1.0;
#endif

#if BLOOM
  float3 lensDirtFinalColor = lensDirtBloomColor / (lensDirtBloomSample.w + 1.0);
  lensDirtFinalColor = max(0.0, lensDirtFinalColor); // Luma: disabled? // TODO!!!
  postProcessedColor += lensDirtFinalColor * lensDirtColor;
#endif // BLOOM

  float3 tonemappedColor = postProcessedColor;

#if 1 // Tonemapping+Grading LUT

#if COLOR_GRADING_TONEMAP_LUT

  float3 lutCoords = EncodeLUTInput(postProcessedColor);
#if 1 // Luma: LUT is now linear, so skew coordinates!
  lutCoords = AdjustLUTCoordinatesForLinearOutputLUT(lutCoords);
#endif
  lutCoords = lutCoords * (1.0 - (1.0 / LUTSize)) + (0.5 / LUTSize); // Acknowledge half texel offset
  tonemappedColor = HDRLighting__ColorRemapHDRTexture.SampleLevel(ColorClamp2D_s, lutCoords, 0).rgb; // Log in, linear out
#if 0 // Luma: optionally changed LUT output encoidng to Log instead of linear...
  tonemappedColor = DecodeLUTInput(tonemappedColor);
#endif
#if 1 // Luma: restore any color the LUT log encoding clipped away! It's actually quite a lot near black.
  bool unclipLUT = true;
  bool skipLUT = false;
#if DEVELOPMENT
  unclipLUT = DVS5 <= 0.0;
  skipLUT = DVS5 >= 0.5;
#endif
  if (LumaSettings.DisplayMode == 1 && unclipLUT) // TODO: test in a dark scene
  {
    float lutClippingPoint = DecodeLUTInput(0.0).x;
    tonemappedColor += min(postProcessedColor, lutClippingPoint); // Note: If the LUT inverted colors, or did a fade to black, though that doesn't usually happen
  }
  if (skipLUT) // Test
    tonemappedColor = postProcessedColor;
#endif

#else // ACES fitted

  postProcessedColor = pow(abs(postProcessedColor), 0.822222173) * sign(postProcessedColor); // Luma: fixed abs() not restoring sign later. I think this is ACES dim room emulation or something
  postProcessedColor = ACESFittedAP1(postProcessedColor); // TODO: HDR here

#if 0 // Luma: disabled saturate
  tonemappedColor = saturate(tonemappedColor);
#endif

#endif // COLOR_GRADING_TONEMAP_LUT

#endif

  // TODO: what is this?
  r2.xy = v0.xy * float2(1.0, TemporalFilteringParams.y * LensParams2.w * LensParams1.w);
  r0.w = dot(r2.xy, r2.xy);
  r0.w = sqrt(r0.w);
  r0.w = -LensParams2.x + r0.w;
  r0.w = r0.w / LensParams2.y;
  r0.w = saturate(0.5 + r0.w);
  r1.x = r0.w * r0.w;
  r0.w = dot(r0.ww, r1.xx);
  r0.w = r1.x * 3 + -r0.w;
  r0.w = -r0.w * LensParams2.z + 1;
  tonemappedColor *= r0.w;

#if ENABLE_DITHER
  tonemappedColor = linear_to_sRGB_gamma(tonemappedColor, GCT_MIRROR); // Luma: fixed negative values support (they were doing abs...) // TODO: do in gamma 2.2 space if necessary? Probably not!
  int2 noiseCoords = GlobalNoiseSampler2DSquareSize.x * frac(GlobalNoiseSampler2DSquareSize.y * v1.xy);
  int ditherBitDepth = 8;
#if 1 // Luma: remove excessive noise
  ditherBitDepth = 9;
#endif
  tonemappedColor += (Global__GlobalNoiseSampler2D__TexObj__.Load(int3(noiseCoords, 0)).xyz - 0.5) / ((2 ^ ditherBitDepth) - 1);
  tonemappedColor = gamma_sRGB_to_linear(tonemappedColor, GCT_MIRROR); // Luma: fixed negative values support (note that negative film grain would flip back to positive due to an abs here, meaning it'd raise blacks!)
#endif // ENABLE_DITHER

  o0.rgb = tonemappedColor;
  // Flipped alpha mask
  o0.w = saturate(1.0 - HDRLighting__PostFxMaskTexture__TexObj__.Load(int3(pixelCoords, 0)).w); // Luma: add saturate to emulate original UNORM behaviour
}