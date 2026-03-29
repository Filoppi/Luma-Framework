#include "./quantum_break_common.hlsli"

// clang-format off
cbuffer cb_update_1 : register(b0) {
  float2 g_vScreenRes : packoffset(c0);
  float2 g_vInvScreenRes : packoffset(c0.z);
  float2 g_vOutputRes : packoffset(c1);
  float2 g_vInvOutputRes : packoffset(c1.z);
  float4x4 g_mWorldToView : packoffset(c2);
  float4x4 g_mViewToWorld : packoffset(c6);
  float4x4 g_mViewToClip : packoffset(c10);
  float4x4 g_mClipToView : packoffset(c14);
  float4x4 g_mWorldToClip : packoffset(c18);
  float4x4 g_mClipToWorld : packoffset(c22);
  float4x4 g_mClipToPreviousClip : packoffset(c26);
  float4x4 g_mViewToPreviousClip : packoffset(c30);
  float4x4 g_mPreviousViewToView : packoffset(c34);
  float4x4 g_mPreviousWorldToClip : packoffset(c38);
  float4x4 g_mPreviousViewToClip : packoffset(c42);
  float4 g_vViewPoint : packoffset(c46);
  float g_fInvNear : packoffset(c47);
  float g_fSimulationTime : packoffset(c47.y);
  float g_fSimulationTimeDelta : packoffset(c47.z);
  float g_fSimulationTimeStep : packoffset(c47.w);
  uint g_uTemporalFrame : packoffset(c48);
  uint g_uCurrentFrame : packoffset(c48.y);

  struct {
    float4 vSunDir;
    float4 vSunE;
    float4 vExtinction;
    float4 vRayleigh;
    float4 vMie;
    float4 vSchlickConstants;
    float4 vFog;
  } g_atmosphere : packoffset(c49);

  float3 g_vFogColor : packoffset(c56);
  float3 g_vFogColorOpposite : packoffset(c57);
  float g_fFogExp : packoffset(c57.w);
  float g_fFogGroundDensityAtViewer : packoffset(c58);
  float g_fFogGroundHeight : packoffset(c58.y);
  float g_fFogGroundFalloff : packoffset(c58.z);
  float g_fFogGroundDensity : packoffset(c58.w);
  float2 g_vFogGroundDensityMapRange : packoffset(c59);
  float3 g_vFogGroundSimulationVelocityAndScale : packoffset(c60);
  uint g_uCharacterLightRigsBindOffset : packoffset(c60.w);
  float4 g_fTileDepthClipRanges[5] : packoffset(c61);
  float4 g_fTileDepthRanges[5] : packoffset(c66);
  float2 g_vDepthTileResolve : packoffset(c71);
  uint g_uDepthTileCount : packoffset(c71.z);
  uint2 g_vTileResolution : packoffset(c72);
  uint3 g_vTileWidthHeightDepth : packoffset(c73);
  float2 g_vTileResolutionPerScreenResolution : packoffset(c74);
  float2 g_vTileDepthNearFar : packoffset(c74.z);
  uint g_uMaxPointLightsPerTile : packoffset(c75);
  uint g_uMaxSpotLightsPerTile : packoffset(c75.y);
  uint g_uAmbientLightTotalCount : packoffset(c75.z);
  float g_fAmbientEnvIntensity : packoffset(c75.w);
  float g_fAmbientSkyIntensity : packoffset(c76);
  float g_fAmbientLocalIntensity : packoffset(c76.y);
  uint g_uPointLightTotalCount : packoffset(c76.z);
  uint g_uSpotLightTotalCount : packoffset(c76.w);
  uint g_uSunLightTotalCount : packoffset(c77);
  uint g_uAmbientLightEnabled : packoffset(c77.y);
  float g_fEnvReflectionEdgeLength : packoffset(c77.z);
  float g_fEnvReflectionMipCount : packoffset(c77.w);
  float g_fInnerRadius : packoffset(c78);
  float g_fOuterRadius : packoffset(c78.y);
  float g_fFadeout : packoffset(c78.z);
  float4 g_vPlayerViewPosition : packoffset(c79);
  float4 g_vPlayerWorldPosition : packoffset(c80);
  float3 g_vDistortionUpInView : packoffset(c81);
  float3 g_vDistortionUpInWorld : packoffset(c82);
  float4x4 g_mViewToGeomDistortionViewClip : packoffset(c83);
  float4x4 g_mWorldToGeomDistortionViewClip : packoffset(c87);
  float g_fFlakeSpawnThreshold : packoffset(c91);
  float g_fFlakeSpawnProbability : packoffset(c91.y);
  float g_fParticleLifetime : packoffset(c91.z);
  float g_fParticleLifetimeDeviation : packoffset(c91.w);
  float3 g_vParticleVelocity : packoffset(c92);
  float g_fParticleSpeedDeviation : packoffset(c92.w);
  float g_fParticleDirectionDeviation : packoffset(c93);
  float3 g_vParticleDirectionDeviationScale : packoffset(c93.y);
  float g_fParticleEmissionFrequency : packoffset(c94);
  uint4 g_vRandomInts : packoffset(c95);
  float2 g_vHalfResolutionJitter : packoffset(c96);
  float g_fInvEnvironmentMapsPerRow : packoffset(c96.z);
  float g_fEnvironmentMapsPerRow : packoffset(c96.w);
  float g_fEnvironmentMapColSize : packoffset(c97);
  float g_fEnvironmentMapRowSize : packoffset(c97.y);
  float2 g_fInvEnvironmentMapAtlasSize : packoffset(c97.z);
  uint4 g_vVolumeLightDimensions : packoffset(c98);
  float4 g_vVolumeLightProjectionConstants : packoffset(c99);
  float4 g_vHalfResVolumeLightProjectionConstants : packoffset(c100);
  float3 g_vOnePerVolumeLightDimensions : packoffset(c101);
  float2 g_vVolumeLightXYToTileXY : packoffset(c102);
  float3 g_vVolumeLightDepthResolve : packoffset(c103);
  float g_fVolumeLightOnePerDepthMinusOne : packoffset(c103.w);
  float3 g_vVolumeLightNearSplit0Far : packoffset(c104);
  float2 g_vVolumeLightSchlickPhaseConstants : packoffset(c105);
  float g_fVolumeLightKernelWidth : packoffset(c105.z);
  float g_fOnePerTranslucencyKernelCount : packoffset(c105.w);
  float4 g_vTessellation_Density_MaxEdge_MinDst_MaxDst : packoffset(c106);
  float4x4 g_mTessellationWorldToClip : packoffset(c107);
  float3 g_fTessellationViewPosition : packoffset(c111);
  float3 g_fTessellationViewDirection : packoffset(c112);
  float g_fTessellationViewToClip11 : packoffset(c112.w);
  float g_fVignetteExp : packoffset(c113);
  float g_fTonemapKeyValue : packoffset(c113.y);
  float g_fTonemapGamma : packoffset(c113.z);
  float g_fTonemapSaturation : packoffset(c113.w);
  float3 g_vTonemapColorBalanceShadows : packoffset(c114);
  float3 g_vTonemapColorBalanceHighlights : packoffset(c115);
  float2 g_vTonemapLevels : packoffset(c116);
  float g_fTonemapNoiseIntensity : packoffset(c116.z);
  int2 g_vTonemapNoiseOffset : packoffset(c117);
  float2 g_vTonemapChromaticAberration : packoffset(c117.z);
  float g_fTonemapBrightness : packoffset(c118);
  bool g_bUseWBOIT : packoffset(c118.y);
  float2 g_vViewportRes : packoffset(c118.z);
  float2 g_vInvViewportRes : packoffset(c119);
  float2 g_vViewportOffset : packoffset(c119.z);
  float2 g_vShadowMapRes : packoffset(c120);
  float2 g_vShadowMapVSMRes : packoffset(c120.z);
  float2 g_vJitterOffset : packoffset(c121);
  int2 g_vSnapOffset : packoffset(c121.z);
  float g_fGIVolumeIntensity : packoffset(c122);
  float4 g_vScreenToView : packoffset(c123);
  float4x4 g_mViewToPreviousScreen : packoffset(c124);
  float g_fViewVolumeFilterTemporalWeight : packoffset(c128);
  float g_fViewVolumeOpticalThickness : packoffset(c128.y);
  float3 g_vViewVolumeParticipatingMediaColor : packoffset(c129);
  float g_fViewVolumeDebugDepth : packoffset(c129.w);
  float3 g_fViewVolumeDebugDirection : packoffset(c130);
  float3 g_fViewVolumeDebugPosition : packoffset(c131);
  float3 g_vSunDirVS : packoffset(c132);
  float3 g_vSunRightVS : packoffset(c133);
  float3 g_vSunUpVS : packoffset(c134);
  float3 g_vSunColor : packoffset(c135);
};
// clang-format on

cbuffer ssaa : register(b1)
{
   float4x4 g_mSSAAClipToPreviousClip[3] : packoffset(c0);
   float2 g_vSSAAJitterOffset[4] : packoffset(c12);
   float2 g_vTAASourceRes : packoffset(c15.z);
   float2 g_vInvTAASourceRes : packoffset(c16);
};

SamplerState g_sLinearClamp : register(s0);
Texture2D<float4> g_tClipDepth : register(t0);
Texture2D<float4> g_sFilmGrain : register(t1);
Texture2D<float4> g_tColor : register(t2);
Texture2D<float4> g_tPreviousColor[3] : register(t3);

static float2 gl_FragCoord;
static float gl_FragDepth;
static float3 SV_Target;

struct SPIRV_Cross_Input
{
   noperspective float4 gl_FragCoord : SV_Position;
};

struct SPIRV_Cross_Output
{
   float3 SV_Target : SV_Target0;
   float gl_FragDepth : SV_Depth;
};

int cvt_f32_i32(float v)
{
   uint abs_bits = asuint(v) & 0x7fffffffu;
   bool is_nan = abs_bits > 0x7f800000u;
   return is_nan ? 0 : ((v < (-2147483648.0f)) ? int(0x80000000) : ((v > 2147483520.0f) ? 2147483647 : int(v)));
}

void frag_main()
{
   float4 _104 = g_sFilmGrain.Load(
       int3(uint2(uint(cvt_f32_i32(float(uint(cvt_f32_i32(gl_FragCoord.x + float(g_vTonemapNoiseOffset.x)) & 511)))),
                  uint(cvt_f32_i32(float(uint(cvt_f32_i32(gl_FragCoord.y + float(g_vTonemapNoiseOffset.y)) & 511))))),
            0u));

   float _111 = (CUSTOM_GRAIN_TYPE == 0.f) ? g_fTonemapNoiseIntensity : 0.f;

   float _113 = (1.0f - _111) * 0.5f;
   float _117 = (_104.x * _111) + _113;
   float _118 = _113 + (_104.y * _111);
   float _119 = _113 + (_104.z * _111);
   float _124 = g_vInvScreenRes.x;
   float _125 = g_vInvScreenRes.y;
   float4 _140 =
       g_tPreviousColor[0].Sample(g_sLinearClamp, float2(mad(gl_FragCoord.x, _124, g_vSSAAJitterOffset[1].x),
                                                         mad(gl_FragCoord.y, _125, g_vSSAAJitterOffset[1].y)));
   float4 _153 =
       g_tPreviousColor[1].Sample(g_sLinearClamp, float2(mad(gl_FragCoord.x, _124, g_vSSAAJitterOffset[2].x),
                                                         mad(gl_FragCoord.y, _125, g_vSSAAJitterOffset[2].y)));
   float4 _169 =
       g_tPreviousColor[2].Sample(g_sLinearClamp, float2(mad(gl_FragCoord.x, _124, g_vSSAAJitterOffset[3].x),
                                                         mad(gl_FragCoord.y, _125, g_vSSAAJitterOffset[3].y)));
   float _180 = mad(gl_FragCoord.x, _124, g_vSSAAJitterOffset[0].x);
   float _181 = mad(gl_FragCoord.y, _125, g_vSSAAJitterOffset[0].y);
   float2 _183 = float2(_180, _181);
   float4 _185 = g_tColor.Sample(g_sLinearClamp, _183);
   float4 _191 = g_tClipDepth.Sample(g_sLinearClamp, _183);
   float _192 = _191.x;
   gl_FragDepth = _192;
   float _193 = ((_140.x + _153.x) + _169.x) + _185.x;
   float _194 = _185.y + (_169.y + (_140.y + _153.y));
   float _195 = _185.z + (_169.z + (_140.z + _153.z));
   SV_Target.x = (_193 < 2.0f) ? (1.0f - ((mad(_193, -0.25f, 1.0f) * 0.5f) / _117))
                               : ((_193 * 0.25f) / mad(0.5f - _117, 2.0f, 1.0f));
   SV_Target.y = (_194 < 2.0f) ? (1.0f - ((mad(_194, -0.25f, 1.0f) * 0.5f) / _118))
                               : ((_194 * 0.25f) / mad(0.5f - _118, 2.0f, 1.0f));
   SV_Target.z = (_195 < 2.0f) ? (1.0f - ((mad(_195, -0.25f, 1.0f) * 0.5f) / _119))
                               : ((_195 * 0.25f) / mad(0.5f - _119, 2.0f, 1.0f));

   SV_Target.rgb = ApplyDisplayMapAndScale(SV_Target.rgb, gl_FragCoord * g_vInvScreenRes);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
   gl_FragCoord = stage_input.gl_FragCoord.xy;
   frag_main();
   SPIRV_Cross_Output stage_output;
   stage_output.gl_FragDepth = gl_FragDepth;
   stage_output.SV_Target = SV_Target;
   return stage_output;
}
