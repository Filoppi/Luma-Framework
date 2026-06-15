#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#if CUSTOM_LUMA == 1
   #include "../../../Source/Core/includes/shader_types.h"
#else
   #include "../../shader_types.h"
#endif
#endif

namespace CB
{
#if CUSTOM_LUMA == 1
    struct LumaGameSettings
    {
        float GammaInfluence;
        float GammaPerceptualChrominanceCorrect;
        float Exposure;
        float TonemapperRolloffStart;
        float TonemapperMaxExpected;
        
        float PerChannelLuminanceReductionEmulateStrength;
        float PerChannelLuminanceReductionEmulatePeak;
        float PerChannelLuminanceReductionEmulateMakeup;

        float Bloom;
        float LensFlare;
        float SlideLensDirt;
        float ADSSights;
        float XrayOutline;
        float MotionBlur;
        float VolumetricFog;
        float RCAS;
        float IsDLAA;

        float LUTBuilderExpansionChrominance;
        float LUTBuilderExpansionLuminance;
        float LUTBuilderHighlightSat;
        float LUTBuilderHighlightSatHighlightsOnly;

        float LUTBuilderNeutralChrominance;
        float LUTBuilderNeutralHue;
        float LUTBuilderNeutralLuma;

        float LUTBuilderGradeSMH;
        float LUTBuilderGradeTint;
        float LUTBuilderGradeSat;
        
        float PCCHue;
        float PCCChrominance;
        // float PCCPeak;

        float BlackFloorSDRTonemap;
        float BlackFloorLUT;
        
        float CGContrast;
        float CGContrastMidGray;
        float CGSaturation;
        float CGHighlightsStrength;
        float CGHighlightsMidGray;
        float CGShadowsStrength;
        float CGShadowsMidGray;
        
        float MovPeakRatio;
        float MovShoulderPow;
    };
    #define GS_GammaInfluence LumaSettings.GameSettings.GammaInfluence
    #define GS_GammaPerceptualChrominanceCorrect LumaSettings.GameSettings.GammaPerceptualChrominanceCorrect
    #define GS_Exposure LumaSettings.GameSettings.Exposure
    #define GS_TonemapperRolloffStart LumaSettings.GameSettings.TonemapperRolloffStart
    #define GS_TonemapperMaxExpected LumaSettings.GameSettings.TonemapperMaxExpected

    #define GS_PerChannelLuminanceReductionEmulateStrength LumaSettings.GameSettings.PerChannelLuminanceReductionEmulateStrength
    #define GS_PerChannelLuminanceReductionEmulatePeak LumaSettings.GameSettings.PerChannelLuminanceReductionEmulatePeak
    #define GS_PerChannelLuminanceReductionEmulateMakeup LumaSettings.GameSettings.PerChannelLuminanceReductionEmulateMakeup
    
    #define GS_Bloom LumaSettings.GameSettings.Bloom
    #define GS_LensFlare LumaSettings.GameSettings.LensFlare
    #define GS_SlideLensDirt LumaSettings.GameSettings.SlideLensDirt
    #define GS_ADSSights LumaSettings.GameSettings.ADSSights
    #define GS_XrayOutline LumaSettings.GameSettings.XrayOutline
    #define GS_MotionBlur LumaSettings.GameSettings.MotionBlur
    #define GS_VolumetricFog LumaSettings.GameSettings.VolumetricFog
    #define GS_RCAS LumaSettings.GameSettings.RCAS
    #define GS_IsDLAA LumaSettings.GameSettings.IsDLAA

    #define GS_LUTBuilderExpansionChrominance LumaSettings.GameSettings.LUTBuilderExpansionChrominance
    #define GS_LUTBuilderExpansionLuminance LumaSettings.GameSettings.LUTBuilderExpansionLuminance
    #define GS_LUTBuilderHighlightSat LumaSettings.GameSettings.LUTBuilderHighlightSat
    #define GS_LUTBuilderHighlightSatHighlightsOnly LumaSettings.GameSettings.LUTBuilderHighlightSatHighlightsOnly
    
    #define GS_LUTBuilderNeutralChrominance LumaSettings.GameSettings.LUTBuilderNeutralChrominance
    #define GS_LUTBuilderNeutralHue LumaSettings.GameSettings.LUTBuilderNeutralHue
    #define GS_LUTBuilderNeutralLuma LumaSettings.GameSettings.LUTBuilderNeutralLuma

    #define GS_LUTBuilderGradeSMH LumaSettings.GameSettings.LUTBuilderGradeSMH
    #define GS_LUTBuilderGradeTint LumaSettings.GameSettings.LUTBuilderGradeTint
    #define GS_LUTBuilderGradeSat LumaSettings.GameSettings.LUTBuilderGradeSat

    #define GS_PCCHue LumaSettings.GameSettings.PCCHue
    #define GS_PCCChrominance LumaSettings.GameSettings.PCCChrominance
    // #define GS_PCCPeak LumaSettings.GameSettings.PCCPeak

    #define GS_BlackFloorSDRTonemap LumaSettings.GameSettings.BlackFloorSDRTonemap
    #define GS_BlackFloorLUT LumaSettings.GameSettings.BlackFloorLUT

    #define GS_CGContrast LumaSettings.GameSettings.CGContrast
    #define GS_CGContrastMidGray LumaSettings.GameSettings.CGContrastMidGray
    #define GS_CGSaturation LumaSettings.GameSettings.CGSaturation
    #define GS_CGHighlightsStrength LumaSettings.GameSettings.CGHighlightsStrength
    #define GS_CGHighlightsMidGray LumaSettings.GameSettings.CGHighlightsMidGray
    #define GS_CGShadowsStrength LumaSettings.GameSettings.CGShadowsStrength
    #define GS_CGShadowsMidGray LumaSettings.GameSettings.CGShadowsMidGray

    #define GS_MovPeakRatio LumaSettings.GameSettings.MovPeakRatio
    #define GS_MovShoulderPow LumaSettings.GameSettings.MovShoulderPow
#else
    struct LumaGameSettings
    {
        float PeakWhiteNits;
        float GamePaperWhiteNits;
        float UIPaperWhiteNits;
        float GammaCorrection;

        float TonemapperMaxExpected;
        // float PerChannelLuminanceReductionEmulateStrength;

        float Bloom;
        float LensFlare;
        float SlideLensDirt;
        float ADSSights;
        float XrayOutline;
        float MotionBlur;
        float VolumetricFog;
        float RCAS;
        
        float BlackFloorSDRTonemap;
    };
    #define GS_PeakWhiteNits CB::shader_injection.PeakWhiteNits
    #define GS_GamePaperWhiteNits CB::shader_injection.GamePaperWhiteNits
    #define GS_UIPaperWhiteNits CB::shader_injection.UIPaperWhiteNits
    #define GS_GammaCorrection CB::shader_injection.GammaCorrection

    #define GS_GammaInfluence 1
    #define GS_GammaPerceptualChrominanceCorrect 0.21
    #define GS_Exposure 1
    #define GS_TonemapperRolloffStart 0
    #define GS_TonemapperMaxExpected CB::shader_injection.TonemapperMaxExpected

    // #define GS_PerChannelLuminanceReductionEmulateStrength CB::shader_injection.PerChannelLuminanceReductionEmulateStrength
    // #define GS_PerChannelLuminanceReductionEmulatePeak 1.3
    // #define GS_PerChannelLuminanceReductionEmulateMakeup 1.2
    
    #define GS_Bloom CB::shader_injection.Bloom
    #define GS_LensFlare CB::shader_injection.LensFlare
    #define GS_SlideLensDirt CB::shader_injection.SlideLensDirt
    #define GS_ADSSights CB::shader_injection.ADSSights
    #define GS_XrayOutline CB::shader_injection.XrayOutline
    #define GS_MotionBlur CB::shader_injection.MotionBlur
    #define GS_VolumetricFog CB::shader_injection.VolumetricFog
    #define GS_RCAS CB::shader_injection.RCAS
    #define GS_IsDLAA 1

    #define GS_LUTBuilderExpansionChrominance 0
    #define GS_LUTBuilderExpansionLuminance 0
    #define GS_LUTBuilderHighlightSat 0
    #define GS_LUTBuilderHighlightSatHighlightsOnly 0
    
    #define GS_LUTBuilderNeutralChrominance 0
    #define GS_LUTBuilderNeutralHue 0
    #define GS_LUTBuilderNeutralLuma 0

    #define GS_LUTBuilderGradeSMH 0
    #define GS_LUTBuilderGradeTint 0
    #define GS_LUTBuilderGradeSat 0

    #define GS_PCCHue 0.475
    #define GS_PCCChrominance 0.8
    // #define GS_PCCPeak 5.0

    #define GS_BlackFloorSDRTonemap CB::shader_injection.BlackFloorSDRTonemap
    #define GS_BlackFloorLUT 0

    #define GS_CGContrast 0
    #define GS_CGContrastMidGray 0
    #define GS_CGSaturation 0
    #define GS_CGHighlightsStrength 0
    #define GS_CGHighlightsMidGray 0
    #define GS_CGShadowsStrength 0
    #define GS_CGShadowsMidGray 0

    #define GS_MovPeakRatio 1
    #define GS_MovShoulderPow 0

    #ifndef __cplusplus
        // #if ((__SHADER_TARGET_MAJOR == 5 && __SHADER_TARGET_MINOR >= 1) || __SHADER_TARGET_MAJOR >= 6)
            // cbuffer shader_injection : register(b13, space50) {
        // #elif (__SHADER_TARGET_MAJOR < 5) || ((__SHADER_TARGET_MAJOR == 5) && (__SHADER_TARGET_MINOR < 1))
            cbuffer shader_injection : register(b13) {
        // #endif
            LumaGameSettings shader_injection : packoffset(c0);
        }
    #endif
#endif

   struct LumaGameData
   {
        float Dummy;
   };
}

#endif // LUMA_GAME_CB_STRUCTS

// cbuffer PerSceneConsts : register(b0)
// {
//   row_major float4x4 projectionMatrix : packoffset(c0);
//   row_major float4x4 viewMatrix : packoffset(c4);
//   row_major float4x4 viewProjectionMatrix : packoffset(c8);
//   row_major float4x4 inverseProjectionMatrix : packoffset(c12);
//   row_major float4x4 inverseViewMatrix : packoffset(c16);
//   row_major float4x4 inverseViewProjectionMatrix : packoffset(c20);
//   float4 eyeOffset : packoffset(c24);
//   float4 adsZScale : packoffset(c25);
//   float4 hdrControl0 : packoffset(c26);
//   float4 hdrControl1 : packoffset(c27);
//   float4 fogColor : packoffset(c28);
//   float4 fogConsts : packoffset(c29);
//   float4 fogConsts2 : packoffset(c30);
//   float4 fogConsts3 : packoffset(c31);
//   float4 fogConsts4 : packoffset(c32);
//   float4 fogConsts5 : packoffset(c33);
//   float4 fogConsts6 : packoffset(c34);
//   float4 fogConsts7 : packoffset(c35);
//   float4 fogConsts8 : packoffset(c36);
//   float4 fogConsts9 : packoffset(c37);
//   float3 sunFogDir : packoffset(c38);
//   float4 sunFogColor : packoffset(c39);
//   float2 sunFog : packoffset(c40);
//   float4 zNear : packoffset(c41);
//   float3 clothPrimaryTint : packoffset(c42);
//   float3 clothSecondaryTint : packoffset(c43);
//   float4 renderTargetSize : packoffset(c44);
//   float4 upscaledTargetSize : packoffset(c45);
//   float4 materialColor : packoffset(c46);
//   float4 cameraUp : packoffset(c47);
//   float4 cameraLook : packoffset(c48);
//   float4 cameraSide : packoffset(c49);
//   float4 cameraVelocity : packoffset(c50);
//   float4 skyMxR : packoffset(c51);
//   float4 skyMxG : packoffset(c52);
//   float4 skyMxB : packoffset(c53);
//   float4 sunMxR : packoffset(c54);
//   float4 sunMxG : packoffset(c55);
//   float4 sunMxB : packoffset(c56);
//   float4 skyRotationTransition : packoffset(c57);
//   float4 debugColorOverride : packoffset(c58);
//   float4 debugAlphaOverride : packoffset(c59);
//   float4 debugNormalOverride : packoffset(c60);
//   float4 debugSpecularOverride : packoffset(c61);
//   float4 debugGlossOverride : packoffset(c62);
//   float4 debugOcclusionOverride : packoffset(c63);
//   float4 debugStreamerControl : packoffset(c64);
//   float4 emblemLUTSelector : packoffset(c65);
//   float4 colorMatrixR : packoffset(c66);
//   float4 colorMatrixG : packoffset(c67);
//   float4 colorMatrixB : packoffset(c68);
//   float4 gameTime : packoffset(c69);
//   float4 gameTick : packoffset(c70);
//   float4 subpixelOffset : packoffset(c71);
//   float4 viewportDimensions : packoffset(c72);
//   float4 viewSpaceScaleBias : packoffset(c73);
//   float4 ui3dUVSetup0 : packoffset(c74);
//   float4 ui3dUVSetup1 : packoffset(c75);
//   float4 ui3dUVSetup2 : packoffset(c76);
//   float4 ui3dUVSetup3 : packoffset(c77);
//   float4 ui3dUVSetup4 : packoffset(c78);
//   float4 ui3dUVSetup5 : packoffset(c79);
//   float4 clipSpaceLookupScale : packoffset(c80);
//   float4 clipSpaceLookupOffset : packoffset(c81);
//   uint4 computeSpriteControl : packoffset(c82);
//   float4 invBcTexSizes : packoffset(c83);
//   float4 invMaskTexSizes : packoffset(c84);
//   float4 relHDRExposure : packoffset(c85);
//   uint4 triDensityFlags : packoffset(c86);
//   float4 triDensityParams : packoffset(c87);
//   float4 voldecalRevealTextureInfo : packoffset(c88);
//   float4 extraClipPlane0 : packoffset(c89);
//   float4 extraClipPlane1 : packoffset(c90);
//   float4 shaderDebug : packoffset(c91);
//   uint isDepthHack : packoffset(c92);
// }

struct PerSceneConsts
{
  row_major float4x4 projectionMatrix;
  row_major float4x4 viewMatrix;
  row_major float4x4 viewProjectionMatrix;
  row_major float4x4 inverseProjectionMatrix;
  row_major float4x4 inverseViewMatrix;
  row_major float4x4 inverseViewProjectionMatrix;
  float4 eyeOffset;
  float4 adsZScale;
  float4 hdrControl0;
  float4 hdrControl1;
  float4 fogColor;
  float4 fogConsts;
  float4 fogConsts2;
  float4 fogConsts3;
  float4 fogConsts4;
  float4 fogConsts5;
  float4 fogConsts6;
  float4 fogConsts7;
  float4 fogConsts8;
  float4 fogConsts9;
  float3 sunFogDir; float p0;
  float4 sunFogColor;
  float2 sunFog; float2 p1;
  float4 zNear;
  float3 clothPrimaryTint; float p2;
  float3 clothSecondaryTint; float p3;
  float4 renderTargetSize;
  float4 upscaledTargetSize;
  float4 materialColor;
  float4 cameraUp;
  float4 cameraLook;
  float4 cameraSide;
  float4 cameraVelocity;
  float4 skyMxR;
  float4 skyMxG;
  float4 skyMxB;
  float4 sunMxR;
  float4 sunMxG;
  float4 sunMxB;
  float4 skyRotationTransition;
  float4 debugColorOverride;
  float4 debugAlphaOverride;
  float4 debugNormalOverride;
  float4 debugSpecularOverride;
  float4 debugGlossOverride;
  float4 debugOcclusionOverride;
  float4 debugStreamerControl;
  float4 emblemLUTSelector;
  float4 colorMatrixR;
  float4 colorMatrixG;
  float4 colorMatrixB;
  float4 gameTime;
  float4 gameTick;
  float4 subpixelOffset;
  float4 viewportDimensions;
  float4 viewSpaceScaleBias;
  float4 ui3dUVSetup0;
  float4 ui3dUVSetup1;
  float4 ui3dUVSetup2;
  float4 ui3dUVSetup3;
  float4 ui3dUVSetup4;
  float4 ui3dUVSetup5;
  float4 clipSpaceLookupScale;
  float4 clipSpaceLookupOffset;
  uint4 computeSpriteControl;
  float4 invBcTexSizes;
  float4 invMaskTexSizes;
  float4 relHDRExposure;
  uint4 triDensityFlags;
  float4 triDensityParams;
  float4 voldecalRevealTextureInfo;
  float4 extraClipPlane0;
  float4 extraClipPlane1;
  float4 shaderDebug;
  uint isDepthHack;
};