#if CUSTOM_SR == 1
// ---- Created with 3Dmigoto v1.3.16 on Thu Nov 27 11:19:16 2025

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
  float4 subpixelOffset : packoffset(c71); //unfortuanately 0 here

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

SamplerState bilinearSampler_s : register(s0);

Texture2D<float4> colorTex : register(t0);                   //color
Texture2D<float4> temporalHistoryTex1 : register(t6);        //special encoded info
Texture2D<float4> temporalHistoryLumaTex1 : register(t7);    //aggregate
Texture2D<float4> temporalHistoryLumaTex2 : register(t9);    //aggregate
Texture2D<float4> temporalHistoryLumaTex3 : register(t10);   //aggregate
Texture2D<float4> velocityTex0 : register(t11);              //scene motion vectors
Texture2D<float4> velocityTex1 : register(t12);              //prev motion vectors
Texture2D<float4> depthTex : register(t14);                  //depth


// 3Dmigoto declarations
#define cmp -
#include "./common1.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float3 o0 : SV_TARGET0,
  out float o1 : SV_TARGET1)
{
  float4 r0,r1,r2,r3,r4,r5,r6,r7,r8;
  uint4 bitmask, uiDest;
  float4 fDest;

  o0 = colorTex.Sample(bilinearSampler_s, v1.xy).xyz;

  //Gamma Decode from SDR
  #if CUSTOM_SDR > 0
    o0 = pow(o0, 2.2);
  #endif

  //tradeoff in 
  o0 = TonemapHDRAndTradeIn(o0);

  o1 = 1; //for AA temporal, so dont care.
  return;
}
#else
cbuffer cb8_buf : register(b8)
{
    float2 cb8_m0 : packoffset(c0);
    float2 cb8_m1 : packoffset(c0.z);
};

cbuffer cb1_buf : register(b1)
{
    uint4 cb1_m[45] : packoffset(c0);
};

SamplerState s0 : register(s0);
Texture2D<float4> t0 : register(t0);
Texture2D<float4> t6 : register(t6);
Texture2D<float4> t7 : register(t7);
Texture2D<float4> t9 : register(t9);
Texture2D<float4> t10 : register(t10);
Texture2D<float4> t11 : register(t11);
Texture2D<float4> t12 : register(t12);
Texture2D<float4> t14 : register(t14);

static float2 TEXCOORD;
static float3 SV_TARGET;
static float SV_TARGET1;

struct SPIRV_Cross_Input
{
    float4 v0 : SV_POSITION0;
    float2 v1 : TEXCOORD0;
};

struct SPIRV_Cross_Output
{
    float3 SV_TARGET : SV_Target0;
    float SV_TARGET1 : SV_Target1;
};

float dp4_f32(float4 a, float4 b)
{
    precise float _93 = a.x * b.x;
    return mad(a.w, b.w, mad(a.z, b.z, mad(a.y, b.y, _93)));
}

float dp3_f32(float3 a, float3 b)
{
    precise float _79 = a.x * b.x;
    return mad(a.z, b.z, mad(a.y, b.y, _79));
}

float dp2_f32(float2 a, float2 b)
{
    precise float _67 = a.x * b.x;
    return mad(a.y, b.y, _67);
}

void frag_main()
{
    float _112 = asfloat(cb1_m[44u].z);
    float _113 = asfloat(cb1_m[44u].w);
    float _119 = mad(_112, 2.0f, TEXCOORD.x);
    float _120 = mad(_113, 2.0f, TEXCOORD.y);
    float _128 = mad(_112, -2.0f, TEXCOORD.x);
    float _129 = mad(_113, -2.0f, TEXCOORD.y);
    float4 _132 = t14.SampleLevel(s0, float2(_128, _129), 0.0f);
    float _133 = _132.x;
    float4 _136 = t14.SampleLevel(s0, float2(_128, _120), 0.0f);
    float _137 = _136.x;
    float _141 = mad(_113, 0.0f, TEXCOORD.y);
    float4 _144 = t14.SampleLevel(s0, float2(_119, _129), 0.0f);
    float _145 = _144.x;
    float4 _149 = t0.SampleLevel(s0, float2(mad(_112, -1.0f, TEXCOORD.x), _141), 0.0f);
    float _150 = _149.x;
    float _151 = _149.y;
    float _152 = _149.z;
    float4 _156 = t14.SampleLevel(s0, float2(TEXCOORD.x, TEXCOORD.y), 0.0f);
    float _157 = _156.x;
    float _158 = max(max(max(_137, max(t14.SampleLevel(s0, float2(_119, _120), 0.0f).x, _133)), _145), _157);
    bool _159 = _157 == _158;
    bool _161 = _133 == _158;
    bool _162 = _137 == _158;
    bool _164 = _145 == _158;
    float4 _182 = t11.SampleLevel(s0, float2(TEXCOORD.x + (_159 ? 0.0f : (_112 * (((!(_161 || _162)) || _164) ? 2.0f : (-2.0f)))), TEXCOORD.y + (_159 ? 0.0f : (_113 * ((_164 || (!((!_161) || _162))) ? (-2.0f) : 2.0f)))), 0.0f);
    float _183 = _182.x;
    float _184 = _182.y;
    float _185 = abs(_183);
    float _186 = abs(_184);
    float _193 = _185 - 0.5f;
    float _194 = _186 - 0.5f;
    float _203 = (clamp(_193 + _193, 0.0f, 1.0f) * 30.0f) + (min(_185 + _185, 1.0f) * 10.0f);
    float _204 = (clamp(_194 + _194, 0.0f, 1.0f) * 30.0f) + (min(_186 + _186, 1.0f) * 10.0f);
    float _207 = (_183 >= 0.0f) ? _203 : (-_203);
    float _208 = (_184 >= 0.0f) ? _204 : (-_204);
    float _209 = _112 * _207;
    float _210 = _113 * _208;
    float _212 = mad(-_112, _207, TEXCOORD.x);
    float _214 = mad(-_113, _208, TEXCOORD.y);
    float2 _216 = float2(_212, _214);
    float4 _218 = t12.SampleLevel(s0, _216, 0.0f);
    float _219 = _218.x;
    float _220 = _218.y;
    float _221 = abs(_219);
    float _222 = abs(_220);
    float _229 = _221 - 0.5f;
    float _230 = _222 - 0.5f;
    float _239 = (min(_221 + _221, 1.0f) * 10.0f) + (clamp(_229 + _229, 0.0f, 1.0f) * 30.0f);
    float _240 = (clamp(_230 + _230, 0.0f, 1.0f) * 30.0f) + (min(_222 + _222, 1.0f) * 10.0f);
    float _243 = (_219 >= 0.0f) ? _239 : (-_239);
    float _244 = (_220 >= 0.0f) ? _240 : (-_240);
    float _254 = mad(-_112, _243, _212);
    float _256 = mad(-_113, _244, _214);
    float4 _267 = t10.GatherRed(s0, float2(_254 - (_209 + (((_112 * _243) - _209) * 2.0f)), _256 - ((((_113 * _244) - _210) * 2.0f) + _210)));
    float4 _274 = t7.GatherRed(s0, _216);
    float4 _290 = t6.SampleLevel(s0, float2(_212 + cb8_m1.x, _214 + cb8_m1.y), 0.0f);
    float _291 = _290.x;
    float _292 = _290.y;
    float _293 = _290.z;
    float4 _313 = t0.SampleLevel(s0, float2(TEXCOORD.x + cb8_m0.x, TEXCOORD.y + cb8_m0.y), 0.0f);
    float _314 = _313.x;
    float _315 = _313.y;
    float _316 = _313.z;
    float _333 = dp3_f32(float3(asfloat((asint(_314 * 3.0517578125e-05f) >> int(1u)) + 532487669), asfloat((asint(_315 * 3.0517578125e-05f) >> int(1u)) + 532487669), asfloat((asint(_316 * 3.0517578125e-05f) >> int(1u)) + 532487669)), float3(0.2125999927520751953125f, 0.715200006961822509765625f, 0.072200000286102294921875f));
    SV_TARGET1 = _333;
    float _339 = min(max(1.0f - dp4_f32(float4(abs(_274.x - _267.x), abs(_274.y - _267.y), abs(_274.z - _267.z), abs(_274.w - _267.w)), 10.0f.xxxx), 0.0f), max(mad(abs(_333 - t9.SampleLevel(s0, float2(_254, _256), 0.0f).x), -40.0f, 1.0f), 0.0f));
    float _340 = mad(_112, 0.0f, TEXCOORD.x);
    float4 _344 = t0.SampleLevel(s0, float2(_340, mad(_113, -1.0f, TEXCOORD.y)), 0.0f);
    float _345 = _344.x;
    float _346 = _344.y;
    float _347 = _344.z;
    float4 _352 = t0.SampleLevel(s0, float2(_340, mad(_113, 1.0f, TEXCOORD.y)), 0.0f);
    float _353 = _352.x;
    float _354 = _352.y;
    float _355 = _352.z;
    float4 _358 = t0.SampleLevel(s0, float2(mad(_112, 1.0f, TEXCOORD.x), _141), 0.0f);
    float _359 = _358.x;
    float _360 = _358.y;
    float _361 = _358.z;
    float _380 = max(_314, max(_150, max(_359, max(_345, _353))));
    float _381 = max(_315, max(_151, max(_360, max(_346, _354))));
    float _382 = max(_316, max(_152, max(_361, max(_347, _355))));
    float _383 = min(_314, min(_150, min(min(_345, _353), _359)));
    float _384 = min(_315, min(_151, min(_360, min(_346, _354))));
    float _385 = min(_316, min(_152, min(_361, min(_347, _355))));
    float _395 = clamp(_291, _383, min(max(_380, _383), max(_291, _380)));
    float _396 = clamp(_292, _384, min(max(_292, _381), max(_384, _381)));
    float _397 = clamp(_293, _385, min(max(_293, _382), max(_385, _382)));
    float2 _407 = float2(_207, _208);
    float2 _411 = float2(_243, _244);
    float2 _420 = float2(_207 - _243, _208 - _244);
    float _427 = clamp(mad(asfloat((asint(dp2_f32(_420, _420)) >> int(1u)) + 532487669), -0.25f, 1.0f), 0.0f, 1.0f);
    float _430 = min(clamp(40.0f - asfloat((asint(max(dp2_f32(_407, _407), dp2_f32(_411, _411))) >> int(1u)) + 532487669), 0.0f, 1.0f), _427 * _427) * 0.5f;
    SV_TARGET.x = mad(mad(_339, _291 - _395, _395) - _314, _430, _314);
    SV_TARGET.y = mad(mad(_339, _292 - _396, _396) - _315, _430, _315);
    SV_TARGET.z = mad(mad(_339, _293 - _397, _397) - _316, _430, _316);
}

SPIRV_Cross_Output main(SPIRV_Cross_Input stage_input)
{
    TEXCOORD = stage_input.v1;
    frag_main();
    SPIRV_Cross_Output stage_output;
    stage_output.SV_TARGET = SV_TARGET;
    stage_output.SV_TARGET1 = SV_TARGET1;
    return stage_output;
}
#endif
