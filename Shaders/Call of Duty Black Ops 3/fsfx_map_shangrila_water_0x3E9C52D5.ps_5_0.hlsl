// ---- Created with 3Dmigoto v1.3.16 on Sat May 23 18:01:42 2026

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
  float refractionAmount : packoffset(c10.z);
  float2 normalScroll : packoffset(c11);
  float2 normalTile : packoffset(c11.z);
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

SamplerState bilinearSampler_s : register(s1);
Texture2D<float4> frameBuffer : register(t0);
Texture2D<float4> revealMap : register(t6);
Texture2D<float4> colorMap : register(t7);
Texture2D<float4> normalMap : register(t9);


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

  r0.x = revealMap.Sample(bilinearSampler_s, v1.xy).x;
  r0.y = cmp(0.5 < scriptVector0.y);
  r0.z = 1 + -r0.x;
  r0.x = r0.y ? r0.z : r0.x;
  r0.y = saturate(scriptVector0.x * 0.998000026 + 0.00100000005);
  r0.z = 1 + -r0.y;
  r0.w = saturate(alphaRevealRamp);
  r0.y = log2(r0.y);
  r0.y = r0.w * r0.y;
  r0.y = exp2(r0.y);
  r1.x = -r0.y;
  r0.y = log2(r0.z);
  r0.y = r0.w * r0.y;
  r1.y = exp2(r0.y);
  r0.yz = saturate(alphaRevealSoftEdge * r1.xy + r0.zz);
  r0.x = r0.x + -r0.y;
  r0.y = r0.z + -r0.y;
  r0.x = saturate(r0.x / r0.y);
  r0.yz = gameTime.ww * normalScroll.xy;
  r0.yz = v1.xy * normalTile.xy + r0.yz;
  r0.w = cmp(0 < r0.x);
  r1.x = cmp(0 < refractionAmount);
  r0.w = r0.w ? r1.x : 0;
  r1.xy = normalMap.Sample(bilinearSampler_s, r0.yz).xy;
  if (r0.w != 0) {
    r1.zw = renderTargetSize.zw * refractionAmount;
    r1.xy = r1.xy * float2(2,2) + float2(-1,-1);
    r1.xy = r1.xy * r0.xx;
    r1.xy = r1.zw * r1.xy;
  } else {
    r1.xy = float2(0,0);
  }
  r2.xyzw = colorMap.Sample(bilinearSampler_s, r0.yz).xyzw;
  r0.yz = saturate(v1.xy + r1.xy);
  r1.xyzw = frameBuffer.Sample(bilinearSampler_s, r0.yz).xyzw;
  r1.xyz = Trade_Out(r1.xyz);

  r0.yzw = /* float3(3.05175781e-005,3.05175781e-005,3.05175781e-005) * */ r1.xyz;
  r0.x = r2.w * r0.x;
  r3.xyz = r2.xyz * r0.xxx;
  r4.xyz = cmp(r3.xyz < float3(0.5,0.5,0.5));
  r1.xyz = /* float3(6.10351563e-005,6.10351563e-005,6.10351563e-005) */ 2 * r1.xyz;
  r5.xyz = r0.yzw /* * r0.yzw */;
  r6.xyz = -r3.xyz * float3(2,2,2) + float3(1,1,1);
  r5.xyz = r6.xyz * r5.xyz;
  r5.xyz = r1.xyz * r3.xyz + r5.xyz;
  // r0.yzw = sqrt(r0.yzw); 
    r0.yzw = pow(r0.yzw, 0.66f);
  r3.xyz = r3.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r2.xyz = -r2.xyz * r0.xxx + float3(1,1,1);
  r1.xyz = r2.xyz * r1.xyz;
  r0.xyz = r0.yzw * r3.xyz + r1.xyz;
  r0.xyz = r4.xyz ? r5.xyz : r0.xyz;
  o0.xyz = /* float3(32768,32768,32768) * */ r0.xyz;
  o0.xyz = Trade_In(o0.xyz);
  o0.w = r1.w;
  return;
}

/*

//
// Generated by Microsoft (R) HLSL Shader Compiler 9.30.9200.16384
//
//
// Buffer Definitions: 
//
// cbuffer $Globals
// {
//
//   float4 flagParams;                 // Offset:    0 Size:    16 [unused]
//   float4 colorObjMin;                // Offset:   16 Size:    16 [unused]
//   float4 colorObjMax;                // Offset:   32 Size:    16 [unused]
//   float colorObjMinBaseBlend;        // Offset:   48 Size:     4 [unused]
//   float colorObjMaxBaseBlend;        // Offset:   52 Size:     4 [unused]
//   float2 uvScroll;                   // Offset:   56 Size:     8 [unused]
//   float4 detailScale;                // Offset:   64 Size:    16 [unused]
//   float4 detailScale1;               // Offset:   80 Size:    16 [unused]
//   float4 detailScale2;               // Offset:   96 Size:    16 [unused]
//   float4 detailScale3;               // Offset:  112 Size:    16 [unused]
//   float4 alphaRevealParms;           // Offset:  128 Size:    16 [unused]
//   float4 colorDetailScale;           // Offset:  144 Size:    16 [unused]
//   float alphaRevealSoftEdge;         // Offset:  160 Size:     4
//   float alphaRevealRamp;             // Offset:  164 Size:     4
//   float refractionAmount;            // Offset:  168 Size:     4
//   float2 normalScroll;               // Offset:  176 Size:     8
//   float2 normalTile;                 // Offset:  184 Size:     8
//
// }
//
// cbuffer GenericsCBuffer
// {
//
//   float4 scriptVector0;              // Offset:    0 Size:    16
//   float4 scriptVector1;              // Offset:   16 Size:    16 [unused]
//   float4 scriptVector2;              // Offset:   32 Size:    16 [unused]
//   float4 scriptVector3;              // Offset:   48 Size:    16 [unused]
//   float4 scriptVector4;              // Offset:   64 Size:    16 [unused]
//   float4 scriptVector5;              // Offset:   80 Size:    16 [unused]
//   float4 scriptVector6;              // Offset:   96 Size:    16 [unused]
//   float4 scriptVector7;              // Offset:  112 Size:    16 [unused]
//   float4 weaponParam0;               // Offset:  128 Size:    16 [unused]
//   float4 weaponParam1;               // Offset:  144 Size:    16 [unused]
//   float4 weaponParam2;               // Offset:  160 Size:    16 [unused]
//   float4 weaponParam3;               // Offset:  176 Size:    16 [unused]
//   float4 weaponParam4;               // Offset:  192 Size:    16 [unused]
//   float4 weaponParam5;               // Offset:  208 Size:    16 [unused]
//   float4 weaponParam6;               // Offset:  224 Size:    16 [unused]
//   float4 weaponParam7;               // Offset:  240 Size:    16 [unused]
//
// }
//
// cbuffer PerSceneConsts
// {
//
//   row_major float4x4 projectionMatrix;// Offset:    0 Size:    64 [unused]
//   row_major float4x4 viewMatrix;     // Offset:   64 Size:    64 [unused]
//   row_major float4x4 viewProjectionMatrix;// Offset:  128 Size:    64 [unused]
//   row_major float4x4 inverseProjectionMatrix;// Offset:  192 Size:    64 [unused]
//   row_major float4x4 inverseViewMatrix;// Offset:  256 Size:    64 [unused]
//   row_major float4x4 inverseViewProjectionMatrix;// Offset:  320 Size:    64 [unused]
//   float4 eyeOffset;                  // Offset:  384 Size:    16 [unused]
//   float4 adsZScale;                  // Offset:  400 Size:    16 [unused]
//   float4 hdrControl0;                // Offset:  416 Size:    16 [unused]
//   float4 hdrControl1;                // Offset:  432 Size:    16 [unused]
//   float4 fogColor;                   // Offset:  448 Size:    16 [unused]
//   float4 fogConsts;                  // Offset:  464 Size:    16 [unused]
//   float4 fogConsts2;                 // Offset:  480 Size:    16 [unused]
//   float4 fogConsts3;                 // Offset:  496 Size:    16 [unused]
//   float4 fogConsts4;                 // Offset:  512 Size:    16 [unused]
//   float4 fogConsts5;                 // Offset:  528 Size:    16 [unused]
//   float4 fogConsts6;                 // Offset:  544 Size:    16 [unused]
//   float4 fogConsts7;                 // Offset:  560 Size:    16 [unused]
//   float4 fogConsts8;                 // Offset:  576 Size:    16 [unused]
//   float4 fogConsts9;                 // Offset:  592 Size:    16 [unused]
//   float3 sunFogDir;                  // Offset:  608 Size:    12 [unused]
//   float4 sunFogColor;                // Offset:  624 Size:    16 [unused]
//   float2 sunFog;                     // Offset:  640 Size:     8 [unused]
//   float4 zNear;                      // Offset:  656 Size:    16 [unused]
//   float3 clothPrimaryTint;           // Offset:  672 Size:    12 [unused]
//   float3 clothSecondaryTint;         // Offset:  688 Size:    12 [unused]
//   float4 renderTargetSize;           // Offset:  704 Size:    16
//   float4 upscaledTargetSize;         // Offset:  720 Size:    16 [unused]
//   float4 materialColor;              // Offset:  736 Size:    16 [unused]
//   float4 cameraUp;                   // Offset:  752 Size:    16 [unused]
//   float4 cameraLook;                 // Offset:  768 Size:    16 [unused]
//   float4 cameraSide;                 // Offset:  784 Size:    16 [unused]
//   float4 cameraVelocity;             // Offset:  800 Size:    16 [unused]
//   float4 skyMxR;                     // Offset:  816 Size:    16 [unused]
//   float4 skyMxG;                     // Offset:  832 Size:    16 [unused]
//   float4 skyMxB;                     // Offset:  848 Size:    16 [unused]
//   float4 sunMxR;                     // Offset:  864 Size:    16 [unused]
//   float4 sunMxG;                     // Offset:  880 Size:    16 [unused]
//   float4 sunMxB;                     // Offset:  896 Size:    16 [unused]
//   float4 skyRotationTransition;      // Offset:  912 Size:    16 [unused]
//   float4 debugColorOverride;         // Offset:  928 Size:    16 [unused]
//   float4 debugAlphaOverride;         // Offset:  944 Size:    16 [unused]
//   float4 debugNormalOverride;        // Offset:  960 Size:    16 [unused]
//   float4 debugSpecularOverride;      // Offset:  976 Size:    16 [unused]
//   float4 debugGlossOverride;         // Offset:  992 Size:    16 [unused]
//   float4 debugOcclusionOverride;     // Offset: 1008 Size:    16 [unused]
//   float4 debugStreamerControl;       // Offset: 1024 Size:    16 [unused]
//   float4 emblemLUTSelector;          // Offset: 1040 Size:    16 [unused]
//   float4 colorMatrixR;               // Offset: 1056 Size:    16 [unused]
//   float4 colorMatrixG;               // Offset: 1072 Size:    16 [unused]
//   float4 colorMatrixB;               // Offset: 1088 Size:    16 [unused]
//   float4 gameTime;                   // Offset: 1104 Size:    16
//   float4 gameTick;                   // Offset: 1120 Size:    16 [unused]
//   float4 subpixelOffset;             // Offset: 1136 Size:    16 [unused]
//   float4 viewportDimensions;         // Offset: 1152 Size:    16 [unused]
//   float4 viewSpaceScaleBias;         // Offset: 1168 Size:    16 [unused]
//   float4 ui3dUVSetup0;               // Offset: 1184 Size:    16 [unused]
//   float4 ui3dUVSetup1;               // Offset: 1200 Size:    16 [unused]
//   float4 ui3dUVSetup2;               // Offset: 1216 Size:    16 [unused]
//   float4 ui3dUVSetup3;               // Offset: 1232 Size:    16 [unused]
//   float4 ui3dUVSetup4;               // Offset: 1248 Size:    16 [unused]
//   float4 ui3dUVSetup5;               // Offset: 1264 Size:    16 [unused]
//   float4 clipSpaceLookupScale;       // Offset: 1280 Size:    16 [unused]
//   float4 clipSpaceLookupOffset;      // Offset: 1296 Size:    16 [unused]
//   uint4 computeSpriteControl;        // Offset: 1312 Size:    16 [unused]
//   float4 invBcTexSizes;              // Offset: 1328 Size:    16 [unused]
//   float4 invMaskTexSizes;            // Offset: 1344 Size:    16 [unused]
//   float4 relHDRExposure;             // Offset: 1360 Size:    16 [unused]
//   uint4 triDensityFlags;             // Offset: 1376 Size:    16 [unused]
//   float4 triDensityParams;           // Offset: 1392 Size:    16 [unused]
//   float4 voldecalRevealTextureInfo;  // Offset: 1408 Size:    16 [unused]
//   float4 extraClipPlane0;            // Offset: 1424 Size:    16 [unused]
//   float4 extraClipPlane1;            // Offset: 1440 Size:    16 [unused]
//   float4 shaderDebug;                // Offset: 1456 Size:    16 [unused]
//   uint isDepthHack;                  // Offset: 1472 Size:     4 [unused]
//
// }
//
//
// Resource Bindings:
//
// Name                                 Type  Format         Dim      HLSL Bind  Count
// ------------------------------ ---------- ------- ----------- -------------- ------
// bilinearSampler                   sampler      NA          NA             s1      1 
// frameBuffer                       texture  float4          2d             t0      1 
// revealMap                         texture  float4          2d             t6      1 
// colorMap                          texture  float4          2d             t7      1 
// normalMap                         texture  float4          2d             t9      1 
// $Globals                          cbuffer      NA          NA            cb0      1 
// PerSceneConsts                    cbuffer      NA          NA            cb1      1 
// GenericsCBuffer                   cbuffer      NA          NA            cb3      1 
//
//
//
// Input signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_POSITION              0   xyzw        0      POS   float       
// TEXCOORD                 0   xy          1     NONE   float   xy  
//
//
// Output signature:
//
// Name                 Index   Mask Register SysValue  Format   Used
// -------------------- ----- ------ -------- -------- ------- ------
// SV_TARGET                0   xyzw        0   TARGET   float   xyzw
//
      0x00000000: ps_5_0
      0x00000008: dcl_globalFlags refactoringAllowed
      0x0000000C: dcl_constantbuffer CB0[12], immediateIndexed
      0x0000001C: dcl_constantbuffer CB3[1], immediateIndexed
      0x0000002C: dcl_constantbuffer CB1[70], immediateIndexed
      0x0000003C: dcl_sampler s1, mode_default
      0x00000048: dcl_resource_texture2d (float,float,float,float) t0
      0x00000058: dcl_resource_texture2d (float,float,float,float) t6
      0x00000068: dcl_resource_texture2d (float,float,float,float) t7
      0x00000078: dcl_resource_texture2d (float,float,float,float) t9
      0x00000088: dcl_input_ps linear v1.xy
      0x00000094: dcl_output o0.xyzw
      0x000000A0: dcl_temps 7
   0  0x000000A8: sample_indexable(texture2d)(float,float,float,float) r0.x, v1.xyxx, t6.xyzw, s1
   1  0x000000D4: lt r0.y, l(0.500000), cb3[0].y
   2  0x000000F4: add r0.z, -r0.x, l(1.000000)
   3  0x00000114: movc r0.x, r0.y, r0.z, r0.x
   4  0x00000138: mad_sat r0.y, cb3[0].x, l(0.998000), l(0.001000)
   5  0x00000160: add r0.z, -r0.y, l(1.000000)
   6  0x00000180: mov_sat r0.w, cb0[10].y
   7  0x00000198: log r0.y, r0.y
   8  0x000001AC: mul r0.y, r0.y, r0.w
   9  0x000001C8: exp r0.y, r0.y
  10  0x000001DC: mov r1.x, -r0.y
  11  0x000001F4: log r0.y, r0.z
  12  0x00000208: mul r0.y, r0.y, r0.w
  13  0x00000224: exp r1.y, r0.y
  14  0x00000238: mad_sat r0.yz, cb0[10].xxxx, r1.xxyx, r0.zzzz
  15  0x00000260: add r0.x, -r0.y, r0.x
  16  0x00000280: add r0.y, -r0.y, r0.z
  17  0x000002A0: div_sat r0.x, r0.x, r0.y
  18  0x000002BC: mul r0.yz, cb0[11].xxyx, cb1[69].wwww
  19  0x000002E0: mad r0.yz, v1.xxyx, cb0[11].zzwz, r0.yyzy
  20  0x00000308: lt r0.w, l(0.000000), r0.x
  21  0x00000324: lt r1.x, l(0.000000), cb0[10].z
  22  0x00000344: and r0.w, r0.w, r1.x
  23  0x00000360: sample_indexable(texture2d)(float,float,float,float) r1.xy, r0.yzyy, t9.xyzw, s1
  24  0x0000038C: if_nz r0.w
  25  0x00000398:   mul r1.zw, cb0[10].zzzz, cb1[44].zzzw
  26  0x000003BC:   mad r1.xy, r1.xyxx, l(2.000000, 2.000000, 0.000000, 0.000000), l(-1.000000, -1.000000, 0.000000, 0.000000)
  27  0x000003F8:   mul r1.xy, r0.xxxx, r1.xyxx
  28  0x00000414:   mul r1.xy, r1.xyxx, r1.zwzz
  29  0x00000430: else 
  30  0x00000434:   mov r1.xy, l(0,0,0,0)
  31  0x00000454: endif 
  32  0x00000458: sample_indexable(texture2d)(float,float,float,float) r2.xyzw, r0.yzyy, t7.xyzw, s1
  33  0x00000484: add_sat r0.yz, r1.xxyx, v1.xxyx
  34  0x000004A0: sample_indexable(texture2d)(float,float,float,float) r1.xyzw, r0.yzyy, t0.xyzw, s1
  35  0x000004CC: mul r0.yzw, r1.xxyz, l(0.000000, 0.000031, 0.000031, 0.000031)
  36  0x000004F4: mul r0.x, r0.x, r2.w
  37  0x00000510: mul r3.xyz, r0.xxxx, r2.xyzx
  38  0x0000052C: lt r4.xyz, r3.xyzx, l(0.500000, 0.500000, 0.500000, 0.000000)
  39  0x00000554: mul r1.xyz, r1.xyzx, l(0.000061, 0.000061, 0.000061, 0.000000)
  40  0x0000057C: mul r5.xyz, r0.yzwy, r0.yzwy
  41  0x00000598: mad r6.xyz, -r3.xyzx, l(2.000000, 2.000000, 2.000000, 0.000000), l(1.000000, 1.000000, 1.000000, 0.000000)
  42  0x000005D8: mul r5.xyz, r5.xyzx, r6.xyzx
  43  0x000005F4: mad r5.xyz, r1.xyzx, r3.xyzx, r5.xyzx
  44  0x00000618: sqrt r0.yzw, r0.yyzw
  45  0x0000062C: mad r3.xyz, r3.xyzx, l(2.000000, 2.000000, 2.000000, 0.000000), l(-1.000000, -1.000000, -1.000000, 0.000000)
  46  0x00000668: mad r2.xyz, -r2.xyzx, r0.xxxx, l(1.000000, 1.000000, 1.000000, 0.000000)
  47  0x0000069C: mul r1.xyz, r1.xyzx, r2.xyzx
  48  0x000006B8: mad r0.xyz, r0.yzwy, r3.xyzx, r1.xyzx
  49  0x000006DC: movc r0.xyz, r4.xyzx, r5.xyzx, r0.xyzx
  50  0x00000700: mul o0.xyz, r0.xyzx, l(32768.000000, 32768.000000, 32768.000000, 0.000000)
  51  0x00000728: mov o0.w, r1.w
  52  0x0000073C: ret 
// Approximately 53 instruction slots used


*/