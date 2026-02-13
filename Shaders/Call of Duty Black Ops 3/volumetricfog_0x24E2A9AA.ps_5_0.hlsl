// ---- Created with 3Dmigoto v1.3.16 on Thu Oct 16 09:34:19 2025

cbuffer PostFxCBuffer : register(b8)
{
  float4 postFxControl0 : packoffset(c0);
  float4 postFxControl1 : packoffset(c1);
  float4 postFxControl2 : packoffset(c2);
  float4 postFxControl3 : packoffset(c3);
  float4 postFxControl4 : packoffset(c4);
  float4 postFxControl5 : packoffset(c5);
  float4 postFxControl6 : packoffset(c6);
  float4 postFxControl7 : packoffset(c7);
  float4 postFxControl8 : packoffset(c8);
  float4 postFxControl9 : packoffset(c9);
  float4 postFxControlA : packoffset(c10);
  float4 postFxControlB : packoffset(c11);
  float4 postFxControlC : packoffset(c12);
  float4 postFxControlD : packoffset(c13);
  float4 postFxControlE : packoffset(c14);
  float4 postFxControlF : packoffset(c15);
  float4 postFxConst00 : packoffset(c16);
  float4 postFxConst01 : packoffset(c17);
  float4 postFxConst02 : packoffset(c18);
  float4 postFxConst03 : packoffset(c19);
  float4 postFxConst04 : packoffset(c20);
  float4 postFxConst05 : packoffset(c21);
  float4 postFxConst06 : packoffset(c22);
  float4 postFxConst07 : packoffset(c23);
  float4 postFxConst08 : packoffset(c24);
  float4 postFxConst09 : packoffset(c25);
  float4 postFxConst10 : packoffset(c26);
  float4 postFxConst11 : packoffset(c27);
  float4 postFxConst12 : packoffset(c28);
  float4 postFxConst13 : packoffset(c29);
  float4 postFxConst14 : packoffset(c30);
  float4 postFxConst15 : packoffset(c31);
  float4 postFxConst16 : packoffset(c32);
  float4 postFxConst17 : packoffset(c33);
  float4 postFxConst18 : packoffset(c34);
  float4 postFxConst19 : packoffset(c35);
  float4 postFxConst20 : packoffset(c36);
  float4 postFxConst21 : packoffset(c37);
  float4 postFxConst22 : packoffset(c38);
  float4 postFxConst23 : packoffset(c39);
  float4 postFxConst24 : packoffset(c40);
  float4 postFxConst25 : packoffset(c41);
  float4 postFxConst26 : packoffset(c42);
  float4 postFxConst27 : packoffset(c43);
  float4 postFxConst28 : packoffset(c44);
  float4 postFxConst29 : packoffset(c45);
  float4 postFxConst30 : packoffset(c46);
  float4 postFxConst31 : packoffset(c47);
  float4 postFxConst32 : packoffset(c48);
  float4 postFxConst33 : packoffset(c49);
  float4 postFxConst34 : packoffset(c50);
  float4 postFxConst35 : packoffset(c51);
  float4 postFxConst36 : packoffset(c52);
  float4 postFxConst37 : packoffset(c53);
  float4 postFxConst38 : packoffset(c54);
  float4 postFxConst39 : packoffset(c55);
  float4 postFxConst40 : packoffset(c56);
  float4 postFxConst41 : packoffset(c57);
  float4 postFxConst42 : packoffset(c58);
  float4 postFxConst43 : packoffset(c59);
  float4 postFxConst44 : packoffset(c60);
  float4 postFxConst45 : packoffset(c61);
  float4 postFxConst46 : packoffset(c62);
  float4 postFxConst47 : packoffset(c63);
  float4 postFxConst48 : packoffset(c64);
  float4 postFxConst49 : packoffset(c65);
  float4 postFxConst50 : packoffset(c66);
  float4 postFxConst51 : packoffset(c67);
  float4 postFxConst52 : packoffset(c68);
  float4 postFxConst53 : packoffset(c69);
  float4 postFxConst54 : packoffset(c70);
  float4 postFxConst55 : packoffset(c71);
  float4 postFxConst56 : packoffset(c72);
  float4 postFxConst57 : packoffset(c73);
  float4 postFxConst58 : packoffset(c74);
  float4 postFxConst59 : packoffset(c75);
  float4 postFxConst60 : packoffset(c76);
  float4 postFxConst61 : packoffset(c77);
  float4 postFxConst62 : packoffset(c78);
  float4 postFxConst63 : packoffset(c79);
  float4 postFxBloom00 : packoffset(c80);
  float4 postFxBloom01 : packoffset(c81);
  float4 postFxBloom02 : packoffset(c82);
  float4 postFxBloom03 : packoffset(c83);
  float4 postFxBloom04 : packoffset(c84);
  float4 postFxBloom05 : packoffset(c85);
  float4 postFxBloom06 : packoffset(c86);
  float4 postFxBloom07 : packoffset(c87);
  float4 postFxBloom08 : packoffset(c88);
  float4 postFxBloom09 : packoffset(c89);
  float4 postFxBloom10 : packoffset(c90);
  float4 postFxBloom11 : packoffset(c91);
  float4 postFxBloom12 : packoffset(c92);
  float4 postFxBloom13 : packoffset(c93);
  float4 postFxBloom14 : packoffset(c94);
  float4 postFxBloom15 : packoffset(c95);
  float4 postFxBloom16 : packoffset(c96);
  float4 postFxBloom17 : packoffset(c97);
  float4 postFxBloom18 : packoffset(c98);
  float4 postFxBloom19 : packoffset(c99);
  float4 postFxBloom20 : packoffset(c100);
  float4 postFxBloom21 : packoffset(c101);
  float4 postFxBloom22 : packoffset(c102);
  float4 postFxBloom23 : packoffset(c103);
  float4 postFxBloom24 : packoffset(c104);
  float4 postFxBloom25 : packoffset(c105);
  float4 filterTap[8] : packoffset(c106);
  float4 postfxViewMatrix0 : packoffset(c114);
  float4 postfxViewMatrix1 : packoffset(c115);
  float4 postfxViewMatrix2 : packoffset(c116);
  float4 postfxViewMatrix3 : packoffset(c117);
  float4 postfxProjMatrix0 : packoffset(c118);
  float4 postfxProjMatrix1 : packoffset(c119);
  float4 postfxProjMatrix2 : packoffset(c120);
  float4 postfxProjMatrix3 : packoffset(c121);
  float4 postfxViewProjMatrix0 : packoffset(c122);
  float4 postfxViewProjMatrix1 : packoffset(c123);
  float4 postfxViewProjMatrix2 : packoffset(c124);
  float4 postfxViewProjMatrix3 : packoffset(c125);
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

SamplerState volumeLightSamplerState_s : register(s1);
SamplerState depthSamplerState_s : register(s2);
Texture2D<float4> volumeLightTexture : register(t0);
Texture2D<float> downsampledAlphaDepth : register(t2);
Texture2D<float> floatAlphaZSampler : register(t3);
Texture2D<float4> blendTexture : register(t5); //color


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2,r3,r4,r5;
  uint4 bitmask, uiDest;
  float4 fDest;

  //uv
  volumeLightTexture.GetDimensions(0, uiDest.x, uiDest.y, uiDest.z);
  r0.xy = uiDest.xy;
  r0.xy = (uint2)r0.xy;
  r0.xy = rcp(r0.xy);
  r0.zw = float2(1,1) + v0.xy;
  r0.zw = r0.zw / postFxControl7.xx;
  r1.xy = floor(r0.zw);
  r2.xy = frac(r0.zw);
  r0.xy = r1.xy * r0.xy;
  r1.xyzw = downsampledAlphaDepth.Gather(depthSamplerState_s, r0.xy).xyzw;
  r1.xyzw = max(9.99999994e-009, r1.wzxy);
  r1.xyzw = rcp(r1.xyzw);

  //sample
  int3 r3i = int3((int2)v0.xy, 0);
  r0.z = floatAlphaZSampler.Load(r3i.xyz).x;
  r3.xyz = blendTexture.Load(r3i.xyz).xyz;

  //stuff with depth for decode later
  r0.z = max(9.99999994e-009, r0.z);
  r0.z = rcp(r0.z);

  r1.xyzw = r1.xyzw + -r0.z;
  r0.z = postFxControlA.y * r0.z;

  r0.z = 1 / r0.z; //not rcp?

  r1.xyzw = r0.z * abs(r1.xyzw) + 9.99999975e-005;
  r1.xyzw = 1.f / r1.xyzw;

  r1.xyzw = min(1.f, r1.xyzw);

  r2.zw = 1.f + -r2.xy;

  r2.xyzw = r2.zxzx * r2.wwyy;
  r4.xyzw = r2.xyzw * r1.xyzw;

  //uv for volumeLightTexture
  r0.z = r4.x + r4.y;
  r0.z = r1.z * r2.z + r0.z;
  r0.z = r1.w * r2.w + r0.z;

  //FIXED: Gather from dissambly was missing rgb
  float4 vR = volumeLightTexture.GatherRed(volumeLightSamplerState_s, r0.xy).xyzw;
  float4 vG = volumeLightTexture.GatherGreen(volumeLightSamplerState_s, r0.xy).xyzw;
  float4 vB = volumeLightTexture.GatherBlue(volumeLightSamplerState_s, r0.xy).xyzw;
  r1.x = dot(r4.zwyx, vR);
  r1.y = dot(r4.zwyx, vG);
  r1.z = dot(r4.zwyx, vB);

  r0.xyz = r1.xyz / r0.z;
  r0.xyz = relHDRExposure.y * GS.VolumetricFog * r0.xyz;

  //force 0 if too small
  r1.xyz = cmp(r0.xyz >= 6.10351563e-005);
  r0.xyz = r1.xyz ? r0.xyz : 0;

  //clamp max
  r0.xyz = min(float3(65024,65024,64512), r0.xyz); //why not 3 channel same?

  //add composite
  r0.xyz = r0.xyz + r3.xyz;

  o0.xyz = r0.xyz;
  o0.w = 0;
  return;
}

/*
      0x00000000: ps_5_0
      0x00000008: dcl_globalFlags refactoringAllowed
      0x0000000C: dcl_constantbuffer CB8[11], immediateIndexed
      0x0000001C: dcl_constantbuffer CB1[86], immediateIndexed
      0x0000002C: dcl_sampler s1, mode_default
      0x00000038: dcl_sampler s2, mode_default
      0x00000044: dcl_resource_texture2d (float,float,float,float) t0
      0x00000054: dcl_resource_texture2d (float,float,float,float) t2
      0x00000064: dcl_resource_texture2d (float,float,float,float) t3
      0x00000074: dcl_resource_texture2d (float,float,float,float) t5
      0x00000084: dcl_input_ps_siv linear noperspective v0.xy, position
      0x00000094: dcl_output o0.xyzw
      0x000000A0: dcl_temps 6

   0  0x000000A8: resinfo_indexable(texture2d)(float,float,float,float)_uint r0.xy, l(0), t0.xyzw
   1  0x000000CC: utof r0.xy, r0.xyxx
   2  0x000000E0: rcp r0.xy, r0.xyxx
   3  0x000000F4: add r0.zw, v0.xxxy, l(0.000000, 0.000000, 1.000000, 1.000000)
   4  0x0000011C: div r0.zw, r0.zzzw, cb8[7].xxxx
   5  0x0000013C: round_ni r1.xy, r0.zwzz
   6  0x00000150: frc r2.xy, r0.zwzz
   7  0x00000164: mul r0.xy, r0.xyxx, r1.xyxx
   8  0x00000180: gather4_indexable(texture2d)(float,float,float,float) r1.xyzw, r0.xyxx, t2.xyzw, s2.x

   9  0x000001AC: max r1.xyzw, r1.wzxy, l(0.000000, 0.000000, 0.000000, 0.000000)

  10  0x000001D4: rcp r1.xyzw, r1.xyzw

  11  0x000001E8: ftoi r3.xy, v0.xyxx

  12  0x000001FC: mov r3.zw, l(0,0,0,0)

  13  0x0000021C: ld_indexable(texture2d)(float,float,float,float) r0.z, r3.xyww, t3.yzxw
  14  0x00000240: ld_indexable(texture2d)(float,float,float,float) r3.xyz, r3.xyzw, t5.xyzw
  15  0x00000264: max r0.z, r0.z, l(0.000000)
  16  0x00000280: rcp r0.z, r0.z

  17  0x00000294: add r1.xyzw, -r0.zzzz, r1.xyzw
  18  0x000002B4: mul r0.z, r0.z, cb8[10].y

  19  0x000002D4: div r0.z, l(1.000000, 1.000000, 1.000000, 1.000000), r0.z

  20  0x000002FC: mad r1.xyzw, r0.zzzz, |r1.xyzw|, l(0.000100, 0.000100, 0.000100, 0.000100)
  21  0x00000330: div r1.xyzw, l(1.000000, 1.000000, 1.000000, 1.000000), r1.xyzw

  22  0x00000358: min r1.xyzw, r1.xyzw, l(1.000000, 1.000000, 1.000000, 1.000000)

  23  0x00000380: add r2.zw, -r2.xxxy, l(0.000000, 0.000000, 1.000000, 1.000000)

  24  0x000003AC: mul r2.xyzw, r2.wwyy, r2.zxzx

  25  0x000003C8: mul r4.xyzw, r1.xyzw, r2.xyzw
  26  0x000003E4: add r0.z, r4.y, r4.x
  27  0x00000400: mad r0.z, r1.z, r2.z, r0.z
  28  0x00000424: mad r0.z, r1.w, r2.w, r0.z
  
  29  0x00000448: gather4_indexable(texture2d)(float,float,float,float) r1.xyzw, r0.xyxx, t0.xyzw, s1.x
  30  0x00000474: dp4 r1.x, r4.zwyx, r1.xyzw
  31  0x00000490: gather4_indexable(texture2d)(float,float,float,float) r2.xyzw, r0.xyxx, t0.xyzw, s1.y
  32  0x000004BC: gather4_indexable(texture2d)(float,float,float,float) r5.xyzw, r0.xyxx, t0.xyzw, s1.z
  
  33  0x000004E8: dp4 r1.z, r4.zwyx, r5.xyzw
  34  0x00000504: dp4 r1.y, r4.zwyx, r2.xyzw

  35  0x00000520: div r0.xyz, r1.xyzx, r0.zzzz
  36  0x0000053C: mul r0.xyz, r0.xyzx, cb1[85].yyyy

  37  0x0000055C: ge r1.xyz, r0.xyzx, l(0.000061, 0.000061, 0.000061, 0.000000)
  38  0x00000584: and r0.xyz, r0.xyzx, r1.xyzx
  39  0x000005A0: min r0.xyz, r0.xyzx, l(65024.000000, 65024.000000, 64512.000000, 0.000000)
  40  0x000005C8: add o0.xyz, r3.xyzx, r0.xyzx
  41  0x000005E4: mov o0.w, l(0)
  42  0x000005F8: ret 
*/