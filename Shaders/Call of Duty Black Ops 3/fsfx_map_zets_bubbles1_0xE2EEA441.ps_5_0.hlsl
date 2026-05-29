// ---- Created with 3Dmigoto v1.3.16 on Sat May 23 18:26:45 2026

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
  float warp : packoffset(c10.z);
  float speed : packoffset(c10.w);
  float reveal : packoffset(c11);
  float2 tiling : packoffset(c11.y);
  float3 aberrationFraction : packoffset(c12);
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

SamplerState trilinearSampler_s : register(s1);
Texture2D<float4> frameBuffer : register(t0);
Texture2D<float4> revealTexture : register(t6);
Texture2D<float4> warpTexture : register(t7);
Texture2D<float4> rivuletRevealTexture : register(t9);
Texture2D<float4> rivuletWarpTexture : register(t10);


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4;
  uint4 bitmask, uiDest;
  float4 fDest;

  #if CUSTOM_SDR > 0
    LET_THIS_BREAK;
  #endif

  r0.x = saturate(scriptVector0.x);
  r0.x = r0.x * 0.998000026 + 0.00100000005;
  r0.y = log2(r0.x);
  r0.x = 1 + -r0.x;
  r0.z = saturate(alphaRevealRamp);
  r0.y = r0.z * r0.y;
  r0.y = exp2(r0.y);
  r1.x = -r0.y;
  r0.y = log2(r0.x);
  r0.y = r0.z * r0.y;
  r1.y = exp2(r0.y);
  r0.xy = saturate(alphaRevealSoftEdge * r1.xy + r0.xx);
  r0.y = r0.y + -r0.x;
  r0.z = gameTime.w * speed;
  r0.zw = float2(0,-1) * r0.zz;
  r0.zw = v1.xy * tiling.xy + r0.zw;
  r1.x = revealTexture.Sample(trilinearSampler_s, r0.zw).x;
  r0.z = warpTexture.Sample(trilinearSampler_s, r0.zw).x;
  r0.z = r0.z * 2 + -1;
  r0.x = r1.x + -r0.x;
  r0.w = log2(r1.x);
  r0.w = 7 * r0.w;
  r0.w = exp2(r0.w);
  r0.w = reveal * r0.w;
  r0.x = saturate(r0.x / r0.y);
  r0.y = 1 + -alphaRevealSoftEdge;
  r1.x = r0.x + -r0.y;
  r0.y = 1 + -r0.y;
  r0.y = 1 / r0.y;
  r0.y = saturate(r1.x * r0.y);
  r1.x = r0.y * -2 + 3;
  r0.y = r0.y * r0.y;
  r0.y = r1.x * r0.y;
  r1.x = cmp(r0.x >= 0.99000001);
  r0.y = r1.x ? 0 : r0.y;
  r1.x = scriptVector0.z + -scriptVector0.w;
  r1.x = 1 / r1.x;
  r2.x = scriptVector0.y + v1.x;
  r2.y = v1.y;
  r1.y = rivuletRevealTexture.Sample(trilinearSampler_s, r2.xy).x;
  r1.zw = rivuletWarpTexture.Sample(trilinearSampler_s, r2.xy).xy;
  r1.zw = r1.zw * float2(2,2) + float2(-1,-1);
  r1.y = -scriptVector0.w + r1.y;
  r1.x = saturate(r1.y * r1.x);
  r1.y = r1.x * -2 + 3;
  r1.x = r1.x * r1.x;
  r2.x = r1.y * r1.x + -0.800000012;
  r2.x = saturate(10.0000038 * r2.x);
  r2.y = r2.x * -2 + 3;
  r2.x = r2.x * r2.x;
  r2.x = r2.y * r2.x;
  r2.y = r1.y * r1.x;
  r0.x = r1.y * r1.x + r0.x;
  r1.x = cmp(r2.y >= 0.899999976);
  r1.x = r1.x ? 0 : r2.x;
  r0.y = r1.x + r0.y;
  r1.xy = renderTargetSize.zw * warp;
  r2.xz = r1.xy * r0.zz;
  r1.xy = r1.zw * r1.xy + -r2.xz;
  r1.xy = r2.yy * r1.xy + r2.xz;
  r1.zw = v1.xy * float2(2,2) + r1.xy;
  r0.z = dot(r1.xy, r1.xy);
  r0.z = sqrt(r0.z);
  r0.z = 23 * r0.z;
  r0.xyz = min(float3(1,1,1), r0.xyz);
  r2.xyzw = r0.zzzz * float4(-0.470000029,-0.329999983,-0.199999988,0) + float4(1,1,1,1);
  r1.xy = float2(-1,-1) + r1.zw;
  r1.xy = saturate(r1.xy * float2(0.5,0.5) + float2(0.5,0.5));
  r1.zw = renderTargetSize.zw * aberrationFraction.zz;
  r1.zw = r0.yy * r1.zw + r1.xy;
  r3.z = frameBuffer.Sample(trilinearSampler_s, r1.zw).z;
  r4.xyzw = renderTargetSize.zwzw * aberrationFraction.xxyy;
  r1.xyzw = r0.yyyy * r4.xyzw + r1.xyxy;
  r3.x = frameBuffer.Sample(trilinearSampler_s, r1.xy).x;
  r3.y = frameBuffer.Sample(trilinearSampler_s, r1.zw).y;
  
  // r1.xyz = /* float3(3.05175781e-005,3.05175781e-005,3.05175781e-005) * */ r3.xyz;
  r1.xyz = Trade_Out(r3.xyz);

  r3.xyzw = frameBuffer.Sample(trilinearSampler_s, v1.xy).xyzw;
  r3.xyz = Trade_Out(r3.xyz);

  r1.w = r3.w;
  r1.xyzw = r1.xyzw * r2.xyzw + r0.wwww;
  r1.xyzw = -r3.xyzw /* * float4(3.05175781e-005,3.05175781e-005,3.05175781e-005,1) */ + r1.xyzw;
  r2.xyzw = /* float4(3.05175781e-005,3.05175781e-005,3.05175781e-005,1) * */ r3.xyzw;
  r0.y = scriptVector0.x * scriptVector0.x;
  r0.x = r0.x * r0.y;
  r0.xyzw = r0.xxxx * r1.xyzw + r2.xyzw;
  o0.xyzw = /* float4(32768,32768,32768,1) * */ r0.xyzw;
  o0.xyz = Trade_In(o0.xyz);
  return;
}