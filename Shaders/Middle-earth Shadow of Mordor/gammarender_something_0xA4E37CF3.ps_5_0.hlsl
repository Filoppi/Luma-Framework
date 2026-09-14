// ---- Created with 3Dmigoto v1.3.16 on Tue Jul 14 00:47:15 2026

cbuffer _Globals : register(b0)
{
  float DefaultHeight : packoffset(c0) = {100};
  float DefaultWidth : packoffset(c0.y) = {100};

  struct
  {
    float3 m_Position;
    float3 m_Normal;
    float2 m_TexCoord;
    float3 m_Tangent;
    float3 m_Binormal;
    float4 m_Color;
  } MaterialVertexDef_Rigid : packoffset(c1);


  struct
  {
    float3 m_Position;
    float3 m_Normal;
    float2 m_TexCoord;
    float3 m_Tangent;
    float3 m_Binormal;
    float4 m_Color;
    float4 m_Weights;
    float4 m_Indices;
  } MaterialVertexDef_Skeletal : packoffset(c7);

  int k_fSortBias : packoffset(c15) = {100};
  float3 k_cColorOne : packoffset(c15.y) = {0.769999981,0.850000024,1};
  float3 k_cColorTwo : packoffset(c16) = {0.100000001,0.100000001,0.100000001};
  float3 k_cHotHandColorIN : packoffset(c17) = {1,1,1};
  float3 k_cHotHandColorOUT : packoffset(c18) = {0,0,0};
  float k_fAlphaFloor : packoffset(c18.w) = {0.100000001};
  float k_fBrightness : packoffset(c19) = {1};
  float k_fDepthAdjust : packoffset(c19.y) = {1};
  float k_fDesaturate : packoffset(c19.z) = {0};
  float k_fDistortion : packoffset(c19.w) = {0.200000003};
  float k_fHDRScale : packoffset(c20) = {1};
  float k_fHairBendability : packoffset(c20.y) = {0};
  float k_fHairWindBlend : packoffset(c20.z) = {0};
  float k_fHairWindPhase : packoffset(c20.w) = {1};
  float k_fHotHandBrightness : packoffset(c21) = {0};
  float k_fHotHandLerp : packoffset(c21.y) = {0.230000004};
  float k_fHotHandOpacity : packoffset(c21.z) = {0};
  float k_fHotHandPower : packoffset(c21.w) = {3};
  float k_fHotHandWidth : packoffset(c22) = {0.349999994};
  float k_fMaskAmount : packoffset(c22.y) = {1};
  float k_fMaskScaleU : packoffset(c22.z) = {1};
  float k_fMaskScaleV : packoffset(c22.w) = {1};
  float k_fMaskScrollU : packoffset(c23) = {0};
  float k_fMaskScrollV : packoffset(c23.y) = {0};
  float k_fOpacity : packoffset(c23.z) = {1};
  float k_fPanDiffuseU : packoffset(c23.w) = {0};
  float k_fPanDiffuseV : packoffset(c24) = {0};
  float k_fRevealArm : packoffset(c24.y) = {1};
  float k_fRevealWidth : packoffset(c24.z) = {0.100000001};
  float k_fRimAmount : packoffset(c24.w) = {1.12};
  float k_fRimPower : packoffset(c25) = {8};
  float k_fVertAnimScale : packoffset(c25.y) = {0};
  row_major float4x4 k_mObjectToWorld_3DSMAX_ : packoffset(c26);
  float2 k_vHairTimeLag : packoffset(c30) = {0,0};
  float4 k_vHairWindFreqCalm : packoffset(c31) = {0,0,0,0};
  float4 k_vHairWindFreqGust : packoffset(c32) = {0,0,0,0};
  float k_fTime : packoffset(c33);
  row_major float4x4 k_mObjectToClip : packoffset(c34);
  row_major float3x4 k_mObjectToWorld : packoffset(c38);
  float4 k_vFogMieA : packoffset(c41);
  float4 k_vFogMieB : packoffset(c42);
  float4 k_vFogMieC : packoffset(c43);
  float4 k_vFogRayleigh : packoffset(c44);
  float4 k_vFogSky : packoffset(c45);
  float4 k_vFogSunColor : packoffset(c46);
  float3 k_vFogSunDir : packoffset(c47);
  float4 k_vHDRLuminanceWeights : packoffset(c48);
  float4 k_vObjectColor : packoffset(c49);
  float3 k_vObjectSpaceEyePos : packoffset(c50);
  float2 k_vScene_TexCoordScale : packoffset(c51);
  float4 k_vWindDirection : packoffset(c52);
}

SamplerState sDiffuseMapSampler_s : register(s0);
SamplerState sNormalMapSampler_s : register(s1);
SamplerState sSpecularMapSampler_s : register(s2);
Texture2D<float4> tDiffuseMap : register(t0);
Texture2D<float4> tNormalMap : register(t1);
Texture2D<float4> tSpecularMap : register(t2);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  float3 v4 : TEXCOORD3,
  float4 v5 : COLOR0,
  float4 v6 : TEXCOORD4,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = tDiffuseMap.Sample(sDiffuseMapSampler_s, v2.xy).xyzw;
  r0.w = -0.100000001 + r0.w;
  r0.w = cmp(r0.w < 0);
  if (r0.w != 0) discard;
  r0.w = dot(r0.xyz, float3(0.300000012,0.589999974,0.109999999));
  r1.xyz = r0.www + -r0.xyz;
  r0.xyz = k_fDesaturate * r1.xyz + r0.xyz;
  r0.xyz = k_vObjectColor.xyz * r0.xyz;
  r1.xyz = k_fHotHandBrightness * r0.xyz;
  r2.xzw = k_fTime * k_fPanDiffuseU;
  r2.y = k_fTime * k_fPanDiffuseV;
  r2.xyzw = v2.xyxy + r2.xyzw;
  r2.xy = tNormalMap.Sample(sNormalMapSampler_s, r2.xy).yw;
  r2.xy = r2.xy * float2(2,2) + float2(-1,-1);
  r2.xy = k_fDistortion * r2.xy;
  r2.xy = k_fMaskScaleU * r2.zw + r2.xy;
  r0.w = tSpecularMap.Sample(sSpecularMapSampler_s, r2.xy).y;
  r0.w = 1 + -r0.w;
  r0.w = -r0.w * k_fMaskAmount + 1;
  r1.w = dot(v1.xyz, v1.xyz);
  r1.w = rsqrt(r1.w);
  r2.xyz = v1.xyz * r1.www;
  r1.w = dot(v4.xyz, v4.xyz);
  r1.w = rsqrt(r1.w);
  r3.xyz = v4.xyz * r1.www;
  r1.w = saturate(dot(r2.xyz, r3.xyz));
  r2.x = 1.09000003 * r1.w;
  r1.w = k_fRimAmount * r1.w;
  r1.w = log2(r1.w);
  r1.w = k_fRimPower * r1.w;
  r1.w = exp2(r1.w);
  r2.x = log2(r2.x);
  r2.y = 1.25 * k_fRimPower;
  r2.x = r2.y * r2.x;
  r2.x = exp2(r2.x);
  r2.yzw = k_cColorTwo.xyz + -k_cColorOne.xyz;
  r2.yzw = r2.xxx * r2.yzw + k_cColorOne.xyz;
  r0.xyz = r2.yzw * r0.xyz;
  r0.xyz = k_fBrightness * r0.xyz;
  r0.xyz = r0.xyz * r0.www;
  r2.yzw = -k_cHotHandColorOUT.xyz + k_cHotHandColorIN.xyz;
  r2.xyz = r2.xxx * r2.yzw + k_cHotHandColorOUT.xyz;
  r1.xyz = r1.xyz * r2.xyz + -r0.xyz;
  r0.w = 1 + k_fHotHandWidth;
  r0.w = r0.w * k_fHotHandLerp + v5.w;
  r0.w = -1 + r0.w;
  r2.x = 1 / k_fHotHandWidth;
  r0.w = saturate(r2.x * r0.w);
  r2.x = r0.w * -2 + 3;
  r0.w = r0.w * r0.w;
  r0.w = r2.x * r0.w;
  r0.w = log2(r0.w);
  r0.w = k_fHotHandPower * r0.w;
  r0.w = exp2(r0.w);
  r0.w = min(1, r0.w);
  r0.xyz = r0.www * r1.xyz + r0.xyz;
  r1.x = dot(r0.xyz, k_vHDRLuminanceWeights.xyz);
  o0.xyz = r0.xyz * v6.www + v6.xyz;
  r0.x = r1.x * k_fHDRScale + r1.w;
  r0.x = max(k_fAlphaFloor, r0.x);
  r0.y = k_fHotHandOpacity + -r0.x;
  r0.x = r0.w * r0.y + r0.x;
  r0.y = 1 + k_fRevealWidth;
  r0.y = r0.y * k_fRevealArm + v5.w;
  r0.y = -1 + r0.y;
  r0.z = 1 / k_fRevealWidth;
  r0.y = saturate(r0.y * r0.z);
  r0.z = r0.y * -2 + 3;
  r0.y = r0.y * r0.y;
  r0.y = r0.z * r0.y;
  r0.y = k_fOpacity * r0.y;
  r0.y = k_vObjectColor.w * r0.y;
  r0.x = r0.x * r0.y;
  r0.y = r0.x * v6.w + -0.00999999978;
  r0.x = v6.w * r0.x;
  o0.w = r0.x;
  r0.x = cmp(r0.y < 0);
  if (r0.x != 0) discard;

  o0 = max(o0, 0);
  o0.w = saturate(o0.w);
  return;
}