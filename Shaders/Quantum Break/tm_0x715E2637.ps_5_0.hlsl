#include "./tonemap.hlsli"

// ---- Created with 3Dmigoto v1.4.1 on Mon Jan 27 21:38:00 2025

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
   }
g_atmosphere:
   packoffset(c49);

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

cbuffer bloomx86_compose : register(b1)
{
   float2 g_vBloomedImageRes : packoffset(c0);
   float g_fLensTextureIntensity : packoffset(c0.z);
}

SamplerState g_sBaseColorCorrectionMap_s : register(s0);
SamplerState g_sOriginalImage_s : register(s1);
SamplerState g_sBloomedImage_s : register(s2);
SamplerState g_sLensTexture_s : register(s3);
Texture2D<float4> g_sOriginalImage : register(t0);
Texture2D<float4> g_sBloomedImage : register(t1);
Texture2D<float4> g_sLensTexture : register(t2);
Texture2D<float4> g_sBaseColorCorrectionMap : register(t3);
Texture2D<float> g_tBrightness : register(t4);
Texture2D<float4> g_tObjectHighlightTexture : register(t5);

// 3Dmigoto declarations
#define cmp -

void main(float2 v0: TEXCOORD0, float4 v1: SV_Position0, out float4 o0: SV_Target0)
{
   float4 r0, r1, r2, r3, r4, r5;
   uint4 bitmask, uiDest;
   float4 fDest;

   r0.xy = float2(0.5, 0.5) / g_vBloomedImageRes.xy;
   r0.zw = -r0.yx;
   r1.xyzw = v0.xyxy + r0.xzwy;
   r2.xyzw = g_sBloomedImage.Sample(g_sBloomedImage_s, r1.xy).xyzw;
   r1.xyzw = g_sBloomedImage.Sample(g_sBloomedImage_s, r1.zw).xyzw;
   r0.zw = v0.xy * g_vBloomedImageRes.xy + float2(-0.5, -0.5);
   r0.zw = frac(r0.zw);
   r3.xy = float2(1, 1) + -r0.wz;
   r3.zw = r3.xy * r0.zw;
   r0.z = r0.z * r0.w;
   r0.w = r3.y * r3.x;
   r2.xyzw = r3.zzzz * r2.xyzw;
   r3.xy = v0.xy + -r0.xy;
   r0.xy = v0.xy + r0.xy;
   r4.xyzw = g_sBloomedImage.Sample(g_sBloomedImage_s, r0.xy).xyzw;
   r5.xyzw = g_sBloomedImage.Sample(g_sBloomedImage_s, r3.xy).xyzw;
   r2.xyzw = r0.wwww * r5.xyzw + r2.xyzw;
   r1.xyzw = r3.wwww * r1.xyzw + r2.xyzw;
   r0.xyzw = r0.zzzz * r4.xyzw + r1.xyzw;
   r1.xyzw = g_sLensTexture.Sample(g_sLensTexture_s, v0.xy).xyzw;
   r1.xyzw = r1.xyzw * r0.xyzw;
   r1.xyzw = r1.xyzw * float4(2, 2, 2, 2) + -r0.xyzw;
   r0.xyzw = g_fLensTextureIntensity * r1.xyzw + r0.xyzw;
   r1.xyzw = g_sOriginalImage.Sample(g_sOriginalImage_s, v0.xy).xyzw;
   r0.xyzw = r1.xyzw + r0.xyzw;
   r1.x = g_tBrightness.Load(int3(1, 0, 0));
   r0.xyz = r1.xxx * r0.xyz;
   o0.w = r0.w;
   r1.xy = float2(-0.5, -0.5) + v0.xy;
   r1.xy = float2(1.70000005, 0.956250012) * r1.xy;
   r0.w = dot(r1.xy, r1.xy);
   r0.w = sqrt(r0.w);
   r0.w = 1 + -r0.w;
   r0.w = max(0, r0.w);
   r0.w = 0.0500000007 + r0.w;
   r0.w = log2(r0.w);
   r0.w = g_fVignetteExp * r0.w;
   r0.w = exp2(r0.w);
   r0.w = 1.04999995 * r0.w;
   r0.w = min(1, r0.w);
   r0.xyz = r0.xyz * r0.www;
   r0.xyz = max(float3(0, 0, 0), r0.xyz);
   r0.xyz = g_fTonemapKeyValue * r0.xyz;
   r0.w = dot(r0.xyz, float3(0.270000011, 0.670000017, 0.0599999987));
   r1.x = r0.w * 0.015625 + 1;
   r0.w = 1 + r0.w;
   r0.w = r1.x / r0.w;
   r0.xyz = saturate(r0.xyz * r0.www);
#if 1
   r0.rgb = SRGBEncodeAndSample2DLUT(r0.rgb, g_sBaseColorCorrectionMap, g_sBaseColorCorrectionMap_s);
#else
   r1.xyz = log2(r0.xyz);
   r1.xyz = float3(0.416666657, 0.416666657, 0.416666657) * r1.xyz;
   r1.xyz = exp2(r1.xyz);
   r1.xyz = r1.xyz * float3(1.05499995, 1.05499995, 1.05499995) + float3(-0.0549999997, -0.0549999997, -0.0549999997);
   r2.xyz = cmp(float3(0.00313080009, 0.00313080009, 0.00313080009) >= r0.xyz);
   r0.xyz = float3(12.9200001, 12.9200001, 12.9200001) * r0.xyz;
   r0.xyz = r2.xyz ? r0.xyz : r1.xyz;
   r0.y = 0.015625 + r0.y;
   r0.y = max(0.015625, r0.y);
   r1.z = min(0.984375, r0.y);
   r0.x = r0.x * 0.03125 + 0.00048828125;
   r0.y = 32 * r0.z;
   r0.x = max(0.00048828125, r0.x);
   r0.z = ceil(r0.y);
   r0.z = 0.03125 * r0.z;
   r0.z = max(0, r0.z);
   r0.xz = min(float2(0.0307617188, 0.96875), r0.xz);
   r1.x = r0.x + r0.z;
   r2.xyz = g_sBaseColorCorrectionMap.Sample(g_sBaseColorCorrectionMap_s, r1.xz).xyz;
   r0.z = floor(r0.y);
   r0.y = frac(r0.y);
   r0.z = 0.03125 * r0.z;
   r0.z = max(0, r0.z);
   r0.z = min(0.96875, r0.z);
   r1.y = r0.x + r0.z;
   r0.xzw = g_sBaseColorCorrectionMap.Sample(g_sBaseColorCorrectionMap_s, r1.yz).xyz;
   r1.xyz = r2.xyz + -r0.xzw;
   r0.xyz = r0.yyy * r1.xyz + r0.xzw;
#endif
   r0.xyz = g_fTonemapBrightness * r0.xyz;
   r1.xy = (uint2)v1.xy;
   r1.zw = float2(0, 0);
   r1.xyzw = g_tObjectHighlightTexture.Load(r1.xyz).xyzw;
   o0.xyz = r0.xyz * r1.www + r1.xyz;
   return;
}
