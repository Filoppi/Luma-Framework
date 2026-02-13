// ---- Created with 3Dmigoto v1.3.16 on Sat Nov 29 11:09:41 2025

cbuffer _Globals : register(b0)
{
  float4 flagParams : packoffset(c0);
  float4 colorObjMin : packoffset(c1);
  float4 colorObjMax : packoffset(c2);
  float colorObjMinBaseBlend : packoffset(c3);
  float colorObjMaxBaseBlend : packoffset(c3.y);
  float2 uvScroll : packoffset(c3.z);
  float4 detailScale : packoffset(c4);
  float4 detailScale1 : packoffset(c5);
  float4 detailScale2 : packoffset(c6);
  float4 detailScale3 : packoffset(c7);
  float4 alphaRevealParms : packoffset(c8);
  float4 colorDetailScale : packoffset(c9);
  float alphaRevealSoftEdge : packoffset(c10);
  float alphaRevealRamp : packoffset(c10.y);
  float warpPixels : packoffset(c10.z);
  float blurPixels : packoffset(c10.w);
  float aberrationPixels : packoffset(c11);
  float abberationMaskRotation : packoffset(c11.y);
  float3 aberrationFactors : packoffset(c12);
  float2 twinkleScroll1 : packoffset(c13);
  float2 twinkleScroll2 : packoffset(c13.z);
  float2 twinkleScale : packoffset(c14);
  float2 wormyScroll : packoffset(c14.z);
  float highlightFalloff : packoffset(c15);
  float highlightBrightness : packoffset(c15.y);
  bool useAlphaBlend : packoffset(c15.z);
}

cbuffer GenericsCBuffer : register(b3)
{
  float4 scriptVector0 : packoffset(c0);
  float4 scriptVector1 : packoffset(c1);
  float4 scriptVector2 : packoffset(c2);
  float4 scriptVector3 : packoffset(c3);
  float4 scriptVector4 : packoffset(c4);
  float4 scriptVector5 : packoffset(c5);
  float4 scriptVector6 : packoffset(c6);
  float4 scriptVector7 : packoffset(c7);
  float4 weaponParam0 : packoffset(c8);
  float4 weaponParam1 : packoffset(c9);
  float4 weaponParam2 : packoffset(c10);
  float4 weaponParam3 : packoffset(c11);
  float4 weaponParam4 : packoffset(c12);
  float4 weaponParam5 : packoffset(c13);
  float4 weaponParam6 : packoffset(c14);
  float4 weaponParam7 : packoffset(c15);
}

cbuffer PerSceneConsts : register(b1)
{
  row_major float4x4 projectionMatrix : packoffset(c0);
  row_major float4x4 viewMatrix : packoffset(c4);
  row_major float4x4 viewProjectionMatrix : packoffset(c8);
  row_major float4x4 inverseProjectionMatrix : packoffset(c12);
  row_major float4x4 inverseViewMatrix : packoffset(c16);
  row_major float4x4 inverseViewProjectionMatrix : packoffset(c20);
  float4 eyeOffset : packoffset(c24);
  float4 adsZScale : packoffset(c25);
  float4 hdrControl0 : packoffset(c26);
  float4 hdrControl1 : packoffset(c27);
  float4 fogColor : packoffset(c28);
  float4 fogConsts : packoffset(c29);
  float4 fogConsts2 : packoffset(c30);
  float4 fogConsts3 : packoffset(c31);
  float4 fogConsts4 : packoffset(c32);
  float4 fogConsts5 : packoffset(c33);
  float4 fogConsts6 : packoffset(c34);
  float4 fogConsts7 : packoffset(c35);
  float4 fogConsts8 : packoffset(c36);
  float4 fogConsts9 : packoffset(c37);
  float3 sunFogDir : packoffset(c38);
  float4 sunFogColor : packoffset(c39);
  float2 sunFog : packoffset(c40);
  float4 zNear : packoffset(c41);
  float3 clothPrimaryTint : packoffset(c42);
  float3 clothSecondaryTint : packoffset(c43);
  float4 renderTargetSize : packoffset(c44);
  float4 upscaledTargetSize : packoffset(c45);
  float4 materialColor : packoffset(c46);
  float4 cameraUp : packoffset(c47);
  float4 cameraLook : packoffset(c48);
  float4 cameraSide : packoffset(c49);
  float4 cameraVelocity : packoffset(c50);
  float4 skyMxR : packoffset(c51);
  float4 skyMxG : packoffset(c52);
  float4 skyMxB : packoffset(c53);
  float4 sunMxR : packoffset(c54);
  float4 sunMxG : packoffset(c55);
  float4 sunMxB : packoffset(c56);
  float4 skyRotationTransition : packoffset(c57);
  float4 debugColorOverride : packoffset(c58);
  float4 debugAlphaOverride : packoffset(c59);
  float4 debugNormalOverride : packoffset(c60);
  float4 debugSpecularOverride : packoffset(c61);
  float4 debugGlossOverride : packoffset(c62);
  float4 debugOcclusionOverride : packoffset(c63);
  float4 debugStreamerControl : packoffset(c64);
  float4 emblemLUTSelector : packoffset(c65);
  float4 colorMatrixR : packoffset(c66);
  float4 colorMatrixG : packoffset(c67);
  float4 colorMatrixB : packoffset(c68);
  float4 gameTime : packoffset(c69);
  float4 gameTick : packoffset(c70);
  float4 subpixelOffset : packoffset(c71);
  float4 viewportDimensions : packoffset(c72);
  float4 viewSpaceScaleBias : packoffset(c73);
  float4 ui3dUVSetup0 : packoffset(c74);
  float4 ui3dUVSetup1 : packoffset(c75);
  float4 ui3dUVSetup2 : packoffset(c76);
  float4 ui3dUVSetup3 : packoffset(c77);
  float4 ui3dUVSetup4 : packoffset(c78);
  float4 ui3dUVSetup5 : packoffset(c79);
  float4 clipSpaceLookupScale : packoffset(c80);
  float4 clipSpaceLookupOffset : packoffset(c81);
  uint4 computeSpriteControl : packoffset(c82);
  float4 invBcTexSizes : packoffset(c83);
  float4 invMaskTexSizes : packoffset(c84);
  float4 relHDRExposure : packoffset(c85);
  uint4 triDensityFlags : packoffset(c86);
  float4 triDensityParams : packoffset(c87);
  float4 voldecalRevealTextureInfo : packoffset(c88);
  float4 extraClipPlane0 : packoffset(c89);
  float4 extraClipPlane1 : packoffset(c90);
  float4 shaderDebug : packoffset(c91);
  uint isDepthHack : packoffset(c92);
}

SamplerState bilinearClampler_s : register(s1);
SamplerState bilinearSampler_s : register(s2);
Texture2D<float4> frameBuffer : register(t0);
Texture2D<float4> revealMap : register(t6);
Texture2D<float4> colorMap : register(t7);
Texture2D<float4> aberrationMask : register(t9);
Texture2D<float4> twinkleMap : register(t10);
Texture2D<float4> wormMap : register(t11);


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  uint4 bitmask, uiDest;
  float4 fDest;

  #if CUSTOM_SDR > 0
    LET_THIS_BREAK;
  #endif

  r0.xyzw = colorMap.Sample(bilinearClampler_s, v1.xy).xyzw;
  r1.xy = saturate(scriptVector0.zy);
  r0.w = r1.x * r0.w;
  r1.x = revealMap.Sample(bilinearClampler_s, v1.xy).x;
  r1.x = 1 + -r1.x;
  r1.z = min(1, abs(scriptVector0.x));
  r1.zw = warpPixels * r1.zz;
  r1.z = renderTargetSize.z * r1.z;
  r2.xy = v1.xy * float2(2,2) + float2(-1,-1);
  r2.z = cmp(0 < scriptVector0.x);
  r2.w = r1.x * r1.z + 1;
  r3.xy = r2.xy * r2.ww;
  r3.xy = r3.xy * float2(0.5,0.5) + float2(0.5,0.5);
  r1.x = -r1.x * r1.z + 1;
  r1.xz = r2.xy * r1.xx;
  r1.xz = r1.xz * float2(0.5,0.5) + float2(0.5,0.5);
  r1.xz = r2.zz ? r3.xy : r1.xz;
  r2.z = abberationMaskRotation * gameTime.w + 1;
  r2.z = 0.0174532924 * r2.z;
  sincos(r2.z, r3.x, r4.x);
  r4.y = r4.x;
  r4.z = r3.x;
  r5.x = dot(r4.yz, r2.xy);
  r4.x = -r3.x;
  r5.y = dot(r4.xy, r2.xy);
  r2.zw = r5.xy * float2(0.5,0.5) + float2(0.5,0.5);
  r2.z = aberrationMask.Sample(bilinearClampler_s, r2.zw).x;
  r1.w = r2.z * r1.w;
  r1.w = renderTargetSize.z * r1.w;
  r2.w = dot(r2.xy, r2.xy);
  r2.w = rsqrt(r2.w);
  r2.xy = r2.xy * r2.ww;
  r1.y = r2.z * r1.y;
  r1.y = aberrationPixels * r1.y;
  r2.w = 0.170000002 * gameTime.w;
  r2.w = sin(r2.w);
  r2.w = r2.w * 2 + -1;
  r1.y = r2.w * r1.y;
  r3.xy = renderTargetSize.zw * r1.yy;
  r4.xyzw = r3.xyxy * aberrationFactors.xxyy + r1.xzxz;
  r1.yw = r2.xy * r1.ww;
  r1.yw = float2(0.125,0.125) * r1.yw;
  r5.x = 0;
  r2.x = 0;
  while (true) {
    r2.y = cmp((int)r2.x >= 8);
    if (r2.y != 0) break;
    r2.y = (int)r2.x;
    r2.yw = saturate(r1.yw * r2.yy + r4.xy);
    r2.y = frameBuffer.Sample(bilinearClampler_s, r2.yw).x;
    r5.x = r5.x + r2.y;
    r2.x = (int)r2.x + 1;
  }
  r5.y = 0;
  r2.x = 0;
  while (true) {
    r2.y = cmp((int)r2.x >= 8);
    if (r2.y != 0) break;
    r2.y = (int)r2.x;
    r2.yw = saturate(r1.yw * r2.yy + r4.zw);
    r2.y = frameBuffer.Sample(bilinearClampler_s, r2.yw).y;
    r5.y = r5.y + r2.y;
    r2.x = (int)r2.x + 1;
  }
  r1.xz = r3.xy * aberrationFactors.zz + r1.xz;
  r5.z = 0;
  r2.x = 0;
  while (true) {
    r2.y = cmp((int)r2.x >= 8);
    if (r2.y != 0) break;
    r2.y = (int)r2.x;
    r2.yw = saturate(r1.yw * r2.yy + r1.xz);
    r2.y = frameBuffer.Sample(bilinearClampler_s, r2.yw).z;
    r5.z = r5.z + r2.y;
    r2.x = (int)r2.x + 1;
  }
  r1.xy = float2(0.0174532924,0.0174532924) * scriptVector1.yx;
  sincos(r1.xy, r1.xy, r2.xy);
  r3.x = r2.x * r2.y;
  r3.y = r2.y * r1.x;
  r3.z = r1.y;
  r1.x = saturate(dot(cameraLook.xyz, r3.xyz));
  r1.x = log2(r1.x);
  r1.x = highlightFalloff * r1.x;
  r1.x = exp2(r1.x);
  r1.x = highlightBrightness * r1.x;
  r3.xyzw = gameTime.wwww * float4(twinkleScroll1.xy, twinkleScroll2.xy);
  r3.xyzw = v1.xyxy * twinkleScale.xyxy + r3.xyzw;
  r4.xyzw = twinkleMap.Sample(bilinearSampler_s, r3.xy).xyzw;
  r3.xyzw = twinkleMap.Sample(bilinearSampler_s, r3.zw).xyzw;
  r6.xyzw = r3.xyzw + -r4.xyzw;
  r3.xyzw = r3.wwww * r6.xyzw + r4.xyzw;
  r1.yz = gameTime.ww * wormyScroll.xy + v1.xy;
  r4.xyzw = wormMap.Sample(bilinearSampler_s, r1.yz).xyzw;
  r1.y = r3.w * r2.z;
  r1.z = r4.w * r2.z;
  r2.xyz = r3.xyz * r1.yyy;
  r1.yzw = r4.xyz * r1.zzz;

  #if CUSTOM_SDR == 0
    r3.xyz = float3(3.81469727e-006,3.81469727e-006,3.81469727e-006) * r5.xyz;
    r0.xyz = -r5.xyz * float3(3.81469727e-006,3.81469727e-006,3.81469727e-006) + r0.xyz;
  #else
    r3.xyz = r5.xyz;
    r0.xyz = -r5.xyz + r0.xyz;
  #endif

  r0.xyz = r0.www * r0.xyz + r3.xyz;
  r0.xyz = r2.xyz * r1.xxx + r0.xyz;
  r0.xyz = r1.yzw * r1.xxx + r0.xyz;

  o0.xyz = r0.xyz;
  #if CUSTOM_SDR == 0
    o0.xyz *= float3(32768, 32768, 32768);
  #else
    o0.xyz *= 0.125f;
  #endif

  o0.w = 1;
  return;
}