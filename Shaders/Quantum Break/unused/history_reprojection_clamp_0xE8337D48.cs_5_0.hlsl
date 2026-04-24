/*
   Quantum Break history reprojection and clipping prepass.

   Inputs:
   - t0: geometry motion vectors
   - t1: current-frame color used to build a local neighborhood envelope
   - t2-t4: previous temporal history textures

   Work performed:
   - loads a 10x10 neighborhood of current color into groupshared memory
   - computes a local min/max color envelope around each output pixel
   - converts the envelope to xyY space for more stable luminance/chroma clipping
   - reprojects each previous history texture using the motion vectors
   - clamps each reprojected history sample to the current-frame envelope
   - writes the three clamped reprojected histories to u0-u2

   Important:
   - this shader does not output the final anti-aliased image
   - its UAV outputs are consumed later by the final temporal resolve pass
*/
#include "./quantum_break_common.hlsli"

struct _31
{
   float _m0;
   float _m1;
   float _m2;
};

static const _31 _636 = { 0.0f, 0.0f, 0.0f };
static const _31 _34[100] = {
   { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f }
};

// cbuffer cb_update_1 : register(b0)
// {
//    float2 g_vScreenRes : packoffset(c0);
//    float2 g_vInvScreenRes : packoffset(c0.z);
//    float2 g_vOutputRes : packoffset(c1);
//    float2 g_vInvOutputRes : packoffset(c1.z);
//    float4x4 g_mWorldToView : packoffset(c2);
//    float4x4 g_mViewToWorld : packoffset(c6);
//    float4x4 g_mViewToClip : packoffset(c10);
//    float4x4 g_mClipToView : packoffset(c14);
//    float4x4 g_mWorldToClip : packoffset(c18);
//    float4x4 g_mClipToWorld : packoffset(c22);
//    float4x4 g_mClipToPreviousClip : packoffset(c26);
//    float4x4 g_mViewToPreviousClip : packoffset(c30);
//    float4x4 g_mPreviousViewToView : packoffset(c34);
//    float4x4 g_mPreviousWorldToClip : packoffset(c38);
//    float4x4 g_mPreviousViewToClip : packoffset(c42);
//    float4 g_vViewPoint : packoffset(c46);
//    float g_fInvNear : packoffset(c47);
//    float g_fSimulationTime : packoffset(c47.y);
//    float g_fSimulationTimeDelta : packoffset(c47.z);
//    float g_fSimulationTimeStep : packoffset(c47.w);
//    uint g_uTemporalFrame : packoffset(c48);
//    uint g_uCurrentFrame : packoffset(c48.y);

//    // clang-format off
//   struct
//   {
//       float4 vSunDir;
//       float4 vSunE;
//       float4 vExtinction;
//       float4 vRayleigh;
//       float4 vMie;
//       float4 vSchlickConstants;
//       float4 vFog;
//     } g_atmosphere: packoffset(c49);
//    // clang-format on

//    float3 g_vFogColor : packoffset(c56);
//    float3 g_vFogColorOpposite : packoffset(c57);
//    float g_fFogExp : packoffset(c57.w);
//    float g_fFogGroundDensityAtViewer : packoffset(c58);
//    float g_fFogGroundHeight : packoffset(c58.y);
//    float g_fFogGroundFalloff : packoffset(c58.z);
//    float g_fFogGroundDensity : packoffset(c58.w);
//    float2 g_vFogGroundDensityMapRange : packoffset(c59);
//    float3 g_vFogGroundSimulationVelocityAndScale : packoffset(c60);
//    uint g_uCharacterLightRigsBindOffset : packoffset(c60.w);
//    float4 g_fTileDepthClipRanges[5] : packoffset(c61);
//    float4 g_fTileDepthRanges[5] : packoffset(c66);
//    float2 g_vDepthTileResolve : packoffset(c71);
//    uint g_uDepthTileCount : packoffset(c71.z);
//    uint2 g_vTileResolution : packoffset(c72);
//    uint3 g_vTileWidthHeightDepth : packoffset(c73);
//    float2 g_vTileResolutionPerScreenResolution : packoffset(c74);
//    float2 g_vTileDepthNearFar : packoffset(c74.z);
//    uint g_uMaxPointLightsPerTile : packoffset(c75);
//    uint g_uMaxSpotLightsPerTile : packoffset(c75.y);
//    uint g_uAmbientLightTotalCount : packoffset(c75.z);
//    float g_fAmbientEnvIntensity : packoffset(c75.w);
//    float g_fAmbientSkyIntensity : packoffset(c76);
//    float g_fAmbientLocalIntensity : packoffset(c76.y);
//    uint g_uPointLightTotalCount : packoffset(c76.z);
//    uint g_uSpotLightTotalCount : packoffset(c76.w);
//    uint g_uSunLightTotalCount : packoffset(c77);
//    uint g_uAmbientLightEnabled : packoffset(c77.y);
//    float g_fEnvReflectionEdgeLength : packoffset(c77.z);
//    float g_fEnvReflectionMipCount : packoffset(c77.w);
//    float g_fInnerRadius : packoffset(c78);
//    float g_fOuterRadius : packoffset(c78.y);
//    float g_fFadeout : packoffset(c78.z);
//    float4 g_vPlayerViewPosition : packoffset(c79);
//    float4 g_vPlayerWorldPosition : packoffset(c80);
//    float3 g_vDistortionUpInView : packoffset(c81);
//    float3 g_vDistortionUpInWorld : packoffset(c82);
//    float4x4 g_mViewToGeomDistortionViewClip : packoffset(c83);
//    float4x4 g_mWorldToGeomDistortionViewClip : packoffset(c87);
//    float g_fFlakeSpawnThreshold : packoffset(c91);
//    float g_fFlakeSpawnProbability : packoffset(c91.y);
//    float g_fParticleLifetime : packoffset(c91.z);
//    float g_fParticleLifetimeDeviation : packoffset(c91.w);
//    float3 g_vParticleVelocity : packoffset(c92);
//    float g_fParticleSpeedDeviation : packoffset(c92.w);
//    float g_fParticleDirectionDeviation : packoffset(c93);
//    float3 g_vParticleDirectionDeviationScale : packoffset(c93.y);
//    float g_fParticleEmissionFrequency : packoffset(c94);
//    uint4 g_vRandomInts : packoffset(c95);
//    float2 g_vHalfResolutionJitter : packoffset(c96);
//    float g_fInvEnvironmentMapsPerRow : packoffset(c96.z);
//    float g_fEnvironmentMapsPerRow : packoffset(c96.w);
//    float g_fEnvironmentMapColSize : packoffset(c97);
//    float g_fEnvironmentMapRowSize : packoffset(c97.y);
//    float2 g_fInvEnvironmentMapAtlasSize : packoffset(c97.z);
//    uint4 g_vVolumeLightDimensions : packoffset(c98);
//    float4 g_vVolumeLightProjectionConstants : packoffset(c99);
//    float4 g_vHalfResVolumeLightProjectionConstants : packoffset(c100);
//    float3 g_vOnePerVolumeLightDimensions : packoffset(c101);
//    float2 g_vVolumeLightXYToTileXY : packoffset(c102);
//    float3 g_vVolumeLightDepthResolve : packoffset(c103);
//    float g_fVolumeLightOnePerDepthMinusOne : packoffset(c103.w);
//    float3 g_vVolumeLightNearSplit0Far : packoffset(c104);
//    float2 g_vVolumeLightSchlickPhaseConstants : packoffset(c105);
//    float g_fVolumeLightKernelWidth : packoffset(c105.z);
//    float g_fOnePerTranslucencyKernelCount : packoffset(c105.w);
//    float4 g_vTessellation_Density_MaxEdge_MinDst_MaxDst : packoffset(c106);
//    float4x4 g_mTessellationWorldToClip : packoffset(c107);
//    float3 g_fTessellationViewPosition : packoffset(c111);
//    float3 g_fTessellationViewDirection : packoffset(c112);
//    float g_fTessellationViewToClip11 : packoffset(c112.w);
//    float g_fVignetteExp : packoffset(c113);
//    float g_fTonemapKeyValue : packoffset(c113.y);
//    float g_fTonemapGamma : packoffset(c113.z);
//    float g_fTonemapSaturation : packoffset(c113.w);
//    float3 g_vTonemapColorBalanceShadows : packoffset(c114);
//    float3 g_vTonemapColorBalanceHighlights : packoffset(c115);
//    float2 g_vTonemapLevels : packoffset(c116);
//    float g_fTonemapNoiseIntensity : packoffset(c116.z);
//    int2 g_vTonemapNoiseOffset : packoffset(c117);
//    float2 g_vTonemapChromaticAberration : packoffset(c117.z);
//    float g_fTonemapBrightness : packoffset(c118);
//    bool g_bUseWBOIT : packoffset(c118.y);
//    float2 g_vViewportRes : packoffset(c118.z);
//    float2 g_vInvViewportRes : packoffset(c119);
//    float2 g_vViewportOffset : packoffset(c119.z);
//    float2 g_vShadowMapRes : packoffset(c120);
//    float2 g_vShadowMapVSMRes : packoffset(c120.z);
//    float2 g_vJitterOffset : packoffset(c121);
//    int2 g_vSnapOffset : packoffset(c121.z);
//    float g_fGIVolumeIntensity : packoffset(c122);
//    float4 g_vScreenToView : packoffset(c123);
//    float4x4 g_mViewToPreviousScreen : packoffset(c124);
//    float g_fViewVolumeFilterTemporalWeight : packoffset(c128);
//    float g_fViewVolumeOpticalThickness : packoffset(c128.y);
//    float3 g_vViewVolumeParticipatingMediaColor : packoffset(c129);
//    float g_fViewVolumeDebugDepth : packoffset(c129.w);
//    float3 g_fViewVolumeDebugDirection : packoffset(c130);
//    float3 g_fViewVolumeDebugPosition : packoffset(c131);
//    float3 g_vSunDirVS : packoffset(c132);
//    float3 g_vSunRightVS : packoffset(c133);
//    float3 g_vSunUpVS : packoffset(c134);
//    float3 g_vSunColor : packoffset(c135);
// }

SamplerState g_sLinearClamp : register(s0);
Texture2D<float4> g_tGeometryVelocityTexture : register(t0);
Texture2D<float4> g_tColorForExtents : register(t1);
Texture2D<float4> g_tPreviousColor[3] : register(t2);
RWTexture2D<float4> g_rwtReprojectTexture[3] : register(u0);

static uint2 gl_WorkGroupID;
static uint2 gl_LocalInvocationID;
struct SPIRV_Cross_Input
{
   uint2 gl_WorkGroupID : SV_GroupID;
   uint2 gl_LocalInvocationID : SV_GroupThreadID;
};

groupshared _31 g0[100];

int cvt_f32_i32(float v)
{
   return isnan(v) ? 0 : ((v < (-2147483648.0f)) ? int(0x80000000) : ((v > 2147483520.0f) ? 2147483647 : int(v)));
}

float dp2_f32(float2 a, float2 b)
{
   precise float _124 = a.x * b.x;
   return mad(a.y, b.y, _124);
}

float dp3_f32(float3 a, float3 b)
{
   precise float _110 = a.x * b.x;
   return mad(a.z, b.z, mad(a.y, b.y, _110));
}

uint cvt_f32_u32(float v)
{
   return (v > 4294967040.0f) ? 4294967295u : uint(max(v, 0.0f));
}

static const float3x3 RGB_TO_XYZ = float3x3(
    0.5141363739967346f, 0.32387858629226685f, 0.16036376357078552f,
    0.2650679945945740f, 0.6702342629432678f, 0.06409157067537308f,
    0.024118799716234207f, 0.12281779944896698f, 0.8444266319274902f);

static const float3x3 XYZ_TO_RGB = float3x3(
    2.5650999546051025f, -1.1664999723434448f, -0.3986000120639801f,
    -1.0217000246047974f, 1.9776999950408936f, 0.04390000179409981f,
    0.07530000060796738f, -0.25429999828338623f, 1.1892000436782837f);

float3 xyYFromRGB(float3 rgb)
{
   float3 xyz = mul(RGB_TO_XYZ, rgb); // X,Y,Z
   float sum = xyz.x + xyz.y + xyz.z;
   if (abs(sum) < 1e-6f)
      return float3(0.3127f, 0.3290f, 0.0f);
   return float3(xyz.x / sum, xyz.y / sum, xyz.y); // x,y,Y
}

float3 RGBFromxyY(float3 xyY)
{
   float x = xyY.x, y = xyY.y, Y = xyY.z;

   if (abs(y) < 1e-6f)
      return 0.0f.xxx;

   float3 xyz = float3((Y * x) / y, Y, (Y * (1.0f - (x + y))) / y);
   return mul(XYZ_TO_RGB, xyz);
}

void comp_main()
{
   if ((gl_LocalInvocationID.x + (gl_LocalInvocationID.y * 8u)) <= 49u)
   {
      uint _163 = (gl_WorkGroupID.y << 3u) - 1u;
      uint _166 = (gl_LocalInvocationID.x << 1u) + (gl_LocalInvocationID.y * 16u);
      uint _167 = _166 / 10u;
      int _172 = int((gl_WorkGroupID.x << 3u) - 1u);
      int _181 = cvt_f32_i32(g_vOutputRes.x);
      int _182 = cvt_f32_i32(g_vOutputRes.y);
      float4 _191 = g_tColorForExtents.Load(int3(
          uint2(uint(clamp(_172 + ((int(_167) * (-10)) + int(_166)), 0, _181)), uint(clamp(int(_167 + _163), 0, _182))),
          0u));
      g0[_166]._m0 = _191.x;
      g0[_166]._m1 = _191.y;
      g0[_166]._m2 = _191.z;
      uint _203 = _166 + 1u;
      uint _204 = _203 / 10u;
      float4 _217 = g_tColorForExtents.Load(int3(
          uint2(uint(clamp(_172 + ((int(_204) * (-10)) + int(_203)), 0, _181)), uint(clamp(int(_204 + _163), 0, _182))),
          0u));
      g0[_203]._m0 = _217.x;
      g0[_203]._m1 = _217.y;
      g0[_203]._m2 = _217.z;
   }
   GroupMemoryBarrierWithGroupSync();
   int _235 = int(gl_LocalInvocationID.x + (gl_WorkGroupID.x * 8u));
   int _236 = int(gl_LocalInvocationID.y + (gl_WorkGroupID.y * 8u));
   if ((cvt_f32_i32(g_vOutputRes.y) <= _236) || (cvt_f32_i32(g_vOutputRes.x) <= _235))
   {
      return;
   }
   float _244 = float(_235) + 0.5f;
   float _245 = float(_236) + 0.5f;
   uint _247 = gl_LocalInvocationID.x + (gl_LocalInvocationID.y * 10u);
   uint _254 = _247 + 11u;
   uint _255 = _247 + 12u;
   uint _256 = _247 + 10u;
   uint _257 = _247 + 21u;
   uint _318 = _247 + 1u;
   uint _319 = _247 + 2u;
   uint _320 = _247 + 20u;
   uint _321 = _247 + 22u;
   float _338 = mad(g0[_254]._m0, 2.0f, -g0[_255]._m0);
   float _340 = mad(g0[_254]._m1, 2.0f, -g0[_255]._m1);
   float _342 = mad(g0[_254]._m2, 2.0f, -g0[_255]._m2);
   float _350 = mad(g0[_254]._m0, 2.0f, -g0[_256]._m0);
   float _352 = mad(g0[_254]._m1, 2.0f, -g0[_256]._m1);
   float _354 = mad(g0[_254]._m2, 2.0f, -g0[_256]._m2);
   float _362 = mad(g0[_254]._m0, 2.0f, -g0[_257]._m0);
   float _364 = mad(g0[_254]._m1, 2.0f, -g0[_257]._m1);
   float _366 = mad(g0[_254]._m2, 2.0f, -g0[_257]._m2);
   float _374 = mad(g0[_254]._m0, 2.0f, -g0[_318]._m0);
   float _376 = mad(g0[_254]._m1, 2.0f, -g0[_318]._m1);
   float _378 = mad(g0[_254]._m2, 2.0f, -g0[_318]._m2);
   float _379 = min(min(min(min(min(min(min(min(g0[_255]._m0, min(g0[_254]._m0, FLT_MAX)),
                                            g0[_256]._m0),
                                        g0[_257]._m0),
                                    g0[_318]._m0),
                                _338),
                            _350),
                        _362),
                    _374);
   float _380 = min(_376,
                    min(_364, min(_352, min(_340, min(g0[_318]._m1,
                                                      min(g0[_257]._m1,
                                                          min(g0[_256]._m1,
                                                              min(g0[_255]._m1,
                                                                  min(g0[_254]._m1, FLT_MAX)))))))));
   float _381 = min(_378,
                    min(_366, min(_354, min(_342, min(g0[_318]._m2,
                                                      min(g0[_257]._m2,
                                                          min(g0[_256]._m2,
                                                              min(g0[_255]._m2,
                                                                  min(g0[_254]._m2, FLT_MAX)))))))));
   float _382 = max(max(max(max(max(max(max(max(g0[_255]._m0, max(g0[_254]._m0, -FLT_MAX)),
                                            g0[_256]._m0),
                                        g0[_257]._m0),
                                    g0[_318]._m0),
                                _338),
                            _350),
                        _362),
                    _374);
   float _383 = max(
       _376,
       max(_364, max(_352, max(_340, max(g0[_318]._m1,
                                         max(g0[_257]._m1,
                                             max(g0[_256]._m1,
                                                 max(g0[_255]._m1,
                                                     max(g0[_254]._m1, -FLT_MAX)))))))));
   float _384 = max(_378,
                    max(_366, max(_354, max(_342, max(g0[_318]._m2,
                                                      max(g0[_257]._m2,
                                                          max(g0[_256]._m2,
                                                              max(g0[_255]._m2,
                                                                  max(g0[_254]._m2, -FLT_MAX)))))))));
   float4 _462 = g_tGeometryVelocityTexture.SampleLevel(g_sLinearClamp, float2(_244 * g_vInvOutputRes.x, g_vInvOutputRes.y * _245), 0.0f);
   float _463 = _462.x;
   float2 _467 = float2(_463, _462.y);
   float2 _473 = float2(mad(_244, g_vInvOutputRes.x, _463), mad(g_vInvOutputRes.y, _245, _462.y));
   float3 _479 = float3((min(min(min(min(_379, g0[_247]._m0), g0[_319]._m0), g0[_320]._m0), g0[_321]._m0) + _379) * 0.5f,
                        (min(g0[_321]._m1, min(g0[_320]._m1, min(min(_380, g0[_247]._m1), g0[_319]._m1))) + _380) * 0.5f,
                        (min(min(g0[_320]._m2, min(g0[_319]._m2, min(_381, g0[_247]._m2))), g0[_321]._m2) + _381) * 0.5f);

   float3 xyY_minEnvelope = xyYFromRGB(_479);

   float3 _491 = float3((_382 + max(max(max(max(_382, g0[_247]._m0), g0[_319]._m0), g0[_320]._m0), g0[_321]._m0)) * 0.5f,
                        (max(g0[_321]._m1, max(g0[_320]._m1, max(max(_383, g0[_247]._m1), g0[_319]._m1))) + _383) * 0.5f,
                        (max(max(g0[_320]._m2, max(g0[_319]._m2, max(_384, g0[_247]._m2))), g0[_321]._m2) + _384) * 0.5f);

   float3 xyY_maxEnvelope = xyYFromRGB(_491);
   float _513 = abs(xyY_maxEnvelope.x - xyY_minEnvelope.x) * 0.5f;
   float _514 = abs(xyY_maxEnvelope.y - xyY_minEnvelope.y) * 0.5f;
   float _515 = (max(mad(sqrt(dp2_f32(_467, _467)), -100000.0f, 4.0f), 1.0f) * abs(xyY_maxEnvelope.z - xyY_minEnvelope.z)) * 0.5f;

   float _528 = (xyY_maxEnvelope.x + xyY_minEnvelope.x) * 0.5f;
   float _529 = (xyY_minEnvelope.y + xyY_maxEnvelope.y) * 0.5f;
   float _530 = (xyY_minEnvelope.z + xyY_maxEnvelope.z) * 0.5f;
   float _531 = _528 - _513;
   float _532 = _529 - _514;
   float _533 = _530 - _515;
   float _534 = _513 + _528;
   float _535 = _529 + _514;
   float _536 = _530 + _515;

   uint2 output_pixel_coord = uint2(cvt_f32_u32(_244), cvt_f32_u32(_245));
   [unroll]
   for (int i = 0; i < 3; ++i)
   {
      float3 previous_color = g_tPreviousColor[i].SampleLevel(g_sLinearClamp, _473, 0.0f).rgb;
      float3 previous_xyY = xyYFromRGB(previous_color);

      float3 clamped_xyY = float3(
          clamp(_531, previous_xyY.x, _534),
          clamp(_532, previous_xyY.y, _535),
          clamp(_533, previous_xyY.z, _536));

      float3 reprojected_rgb = RGBFromxyY(clamped_xyY);
      g_rwtReprojectTexture[i][output_pixel_coord] = float4(reprojected_rgb, reprojected_rgb.r);
   }
}

[numthreads(8, 8, 1)]
void main(SPIRV_Cross_Input stage_input)
{
   gl_WorkGroupID = stage_input.gl_WorkGroupID;
   gl_LocalInvocationID = stage_input.gl_LocalInvocationID;
   comp_main();
}
