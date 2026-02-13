#ifndef LUMA_GAME_CB_STRUCTS
#define LUMA_GAME_CB_STRUCTS

#ifdef __cplusplus
#include "../../../Source/Core/includes/shader_types.h"
#endif

namespace CB
{
    struct LumaGameSettings
    {
        float GammaInfluence;
        float PreExposure;
        float Exposure;
        float TonemapperRolloffStart;
        float Bloom;
        float SlideLensDirt;
        float ADSSights;
        float XrayOutline;
        float MotionBlur;
        float VolumetricFog;
        float RCAS;
        float SDRTonemapFloorRaiseScale;
        float LUT;
        float LUTBuilderExpansionChrominanceCorrect;
        float LUTBuilderExpansionLuminanceCorrect;
        float LUTBuilderHighlightSat;
        float LUTBuilderHighlightSatHighlightsOnly;
        float LUTBuilderNeutral;
        float PCCStrength;
        float PCCHighlightsOnly;
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