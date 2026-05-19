#include "../Includes/Common.hlsl"

cbuffer cb_update_1 : register(b0)
{
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

  struct
  {
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
}

cbuffer skydome : register(b1)
{
  float g_fSkydomeIntensity : packoffset(c0);
  float4 g_vSkydomeMultiplier : packoffset(c1);
}

SamplerState g_sLinearClamp_s : register(s0);
SamplerState g_sAmbientSHTerms_s : register(s1);
SamplerState g_sSkydomeMap_s : register(s2);
Texture2D<float4> g_sSkydomeMap : register(t0);
Texture2D<float4> g_sAmbientSHTerms : register(t1);
Texture3D<float4> g_tParticipatingMedia : register(t2);

#define cmp

void main(
  float4 v0 : TEXCOORD0,
  float2 v1 : TEXCOORD1,
  float4 v2 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8,r9,r10,r11,r12,r13;
  r0.xyzw = g_sSkydomeMap.Sample(g_sSkydomeMap_s, v1.xy).xyzw;
  r0.xyzw = g_vSkydomeMultiplier.xyzw * r0.xyzw;
  r0.xyzw = g_fSkydomeIntensity * r0.xyzw;
  r0.xyzw = g_fAmbientSkyIntensity * r0.xyzw;

#if 1 // Luma: make sky brighter, it was overly dim
  float normalizationPoint = 0.001; // Found empirically
  float fakeHDRIntensity = 0.15;
  r0.xyz = FakeHDR(r0.xyz * 1, normalizationPoint, fakeHDRIntensity) / 1;
#endif

  r1.x = dot(v0.xyz, v0.xyz);
  r1.x = sqrt(r1.x);
  r1.x = -g_vVolumeLightNearSplit0Far.z + r1.x;
  r1.x = max(0, r1.x);
  r1.y = cmp(r1.x != 0.000000);
  if (r1.y != 0) {
    r1.yzw = g_mViewToWorld._m01_m21_m11 * v0.yyy;
    r1.yzw = g_mViewToWorld._m00_m20_m10 * v0.xxx + r1.yzw;
    r1.yzw = g_mViewToWorld._m02_m22_m12 * v0.zzz + r1.yzw;
    r1.yzw = g_mViewToWorld._m03_m23_m13 + r1.yzw;
    r1.yzw = -g_vViewPoint.xzy + r1.yzw;
    r2.x = dot(r1.yzw, r1.yzw);
    r2.x = rsqrt(r2.x);
    r1.yzw = r2.xxx * r1.yzw;
    r2.x = g_vVolumeLightNearSplit0Far.z * r1.w + g_vViewPoint.y;
    r2.x = -g_fFogGroundHeight + r2.x;
    r2.x = -g_fFogGroundFalloff * r2.x;
    r2.x = 1.44269502 * r2.x;
    r2.x = exp2(r2.x);
    r2.y = g_fFogGroundFalloff * r1.w;
    r2.xy = r2.xy * r1.xx;
    r2.z = cmp(0.00999999978 < abs(r2.y));
    r2.w = -1.44269502 * r2.y;
    r2.w = exp2(r2.w);
    r2.w = 1 + -r2.w;
    r2.y = r2.w / r2.y;
    r2.y = r2.x * r2.y;
    r2.x = r2.z ? r2.y : r2.x;
    r2.x = g_fFogGroundDensity * r2.x;
    r2.x = max(0, r2.x);
    r2.yzw = g_atmosphere.vFog.xxx * g_atmosphere.vExtinction.xyz;
    r2.xyz = r2.yzw * r1.xxx + r2.xxx;
    r1.x = dot(g_atmosphere.vSunDir.xzy, r1.yzw);
    r2.w = r1.x * r1.x + 1;
    r2.w = 0.0596830994 * r2.w;
    r1.x = g_atmosphere.vSchlickConstants.y * r1.x + 1;
    r1.x = r1.x * r1.x;
    r1.x = g_atmosphere.vSchlickConstants.x / r1.x;
    r3.xyz = g_atmosphere.vMie.xyz * r1.xxx;
    r3.xyz = g_atmosphere.vRayleigh.xyz * r2.www + r3.xyz;
    r3.xyz = g_atmosphere.vSunE.xyz * r3.xyz;
    if (g_uAmbientLightEnabled != 0) {
      r1.x = 1 + g_atmosphere.vSchlickConstants.y;
      r1.x = r1.x * r1.x;
      r1.x = g_atmosphere.vSchlickConstants.x / r1.x;
      r4.xyz = g_atmosphere.vMie.xyz * r1.xxx;
      r4.xyz = g_atmosphere.vRayleigh.xyz * float3(0.119366206,0.119366206,0.119366206) + r4.xyz;
      r5.xyz = g_sAmbientSHTerms.SampleLevel(g_sAmbientSHTerms_s, float2(0.0555559993,0), 0).xyz;
      r6.xyz = g_sAmbientSHTerms.SampleLevel(g_sAmbientSHTerms_s, float2(0.166666999,0), 0).xyz;
      r7.xyz = g_sAmbientSHTerms.SampleLevel(g_sAmbientSHTerms_s, float2(0.277778,0), 0).xyz;
      r8.xyz = g_sAmbientSHTerms.SampleLevel(g_sAmbientSHTerms_s, float2(0.388889015,0), 0).xyz;
      r9.xyz = g_sAmbientSHTerms.SampleLevel(g_sAmbientSHTerms_s, float2(0.5,0), 0).xyz;
      r10.xyz = g_sAmbientSHTerms.SampleLevel(g_sAmbientSHTerms_s, float2(0.611110985,0), 0).xyz;
      r11.xyz = g_sAmbientSHTerms.SampleLevel(g_sAmbientSHTerms_s, float2(0.722221971,0), 0).xyz;
      r12.xyz = g_sAmbientSHTerms.SampleLevel(g_sAmbientSHTerms_s, float2(0.833333015,0), 0).xyz;
      r13.xyz = g_sAmbientSHTerms.SampleLevel(g_sAmbientSHTerms_s, float2(0.944444001,0), 0).xyz;
      r8.xyz = r8.xyz * -r1.zzz;
      r6.xyz = r6.xyz * -r1.yyy + r8.xyz;
      r6.xyz = r7.xyz * -r1.www + r6.xyz;
      r6.xyz = float3(0.488602519,0.488602519,0.488602519) * r6.xyz;
      r5.xyz = r5.xyz * float3(0.282094777,0.282094777,0.282094777) + r6.xyz;
      r6.xyz = r11.xyz * -r1.yyy;
      r7.xyz = r9.xyz * -r1.yyy;
      r7.xyz = r7.xyz * -r1.www;
      r6.xyz = r6.xyz * -r1.zzz + r7.xyz;
      r7.xyz = r10.xyz * -r1.zzz;
      r6.xyz = r7.xyz * -r1.www + r6.xyz;
      r5.xyz = r6.xyz * float3(1.09254801,1.09254801,1.09254801) + r5.xyz;
      r6.xyz = float3(0.546274006,0.546274006,0.546274006) * r13.xyz;
      r1.xz = r1.zw * r1.zw;
      r1.x = r1.y * r1.y + -r1.x;
      r1.xyw = r6.xyz * r1.xxx + r5.xyz;
      r5.xyz = float3(0.315391988,0.315391988,0.315391988) * r12.xyz;
      r1.z = r1.z * 3 + -1;
      r1.xyz = r5.xyz * r1.zzz + r1.xyw;
      r3.xyz = r4.xyz * r1.xyz + r3.xyz;
    }
    r1.xyz = float3(-1.44269502,-1.44269502,-1.44269502) * r2.xyz;
    r1.xyz = exp2(r1.xyz);
    r2.xyz = float3(1,1,1) + -r1.xyz;
    r2.xyz = r2.xyz * r3.xyz;
  } else {
    r1.xyz = float3(1,1,1);
    r2.xyz = float3(0,0,0);
  }
  r0.xyz = r0.xyz * r1.xyz + r2.xyz;
  r1.x = v0.z / v0.w;
  r2.xy = g_vInvScreenRes.xy * v2.xy;
  r1.y = cmp(g_vVolumeLightNearSplit0Far.y < r1.x);
  r1.x = log2(r1.x);
  r1.x = -r1.x * 0.693147182 + g_vVolumeLightDepthResolve.y;
  r1.x = g_vVolumeLightDepthResolve.x * r1.x;
  r1.x = r1.y ? r1.x : 0;
  r1.x = 0.5 + r1.x;
  r2.z = r1.x / (float)g_vVolumeLightDimensions.z;
  r1.xyzw = g_tParticipatingMedia.Sample(g_sLinearClamp_s, r2.xyz).xyzw;
  o0.xyz = r0.xyz * r1.www + r1.xyz;
  o0.w = r0.w;
}