// ---- Created with 3Dmigoto v1.3.16 on Mon Jul 13 00:47:45 2026

cbuffer _Globals : register(b0)
{
  float DefaultHeight : packoffset(c0) = {100};
  float DefaultWidth : packoffset(c0.y) = {100};

  struct
  {
    float2 m_Position;
  } MaterialVertexDef_Rigid : packoffset(c1);


  struct
  {
    float2 m_Position;
    float4 m_Weights;
    float4 m_Indices;
  } MaterialVertexDef_Skeletal : packoffset(c2);

  bool bHalfPrecision : packoffset(c5) = false;
  float4 k_fBoxBlurParams : packoffset(c6) = {2,1,0,1};
  float3 k_fFarDOFParams : packoffset(c7) = {0,1,0};
  float3 k_fNearDOFParams : packoffset(c8) = {0,1,0};
  float4 k_vSourceBufferSize : packoffset(c9) = {0,0,0,0};
  bool k_bOITFastVolumetricApprox : packoffset(c10);
}

SamplerState sDownsampleLargeSampler_s : register(s0);
SamplerState sDownsampleMediumSampler_s : register(s1);
SamplerState sCurFrameMapSampler_s : register(s2);
SamplerState sDepthMapSampler_s : register(s3);
SamplerState sOITAlphaSrcSampler_s : register(s4);
SamplerState sOITDepthSrcSampler_s : register(s5);
Texture2D<float4> tDownsampleLarge : register(t0);
Texture2D<float4> tDownsampleMedium : register(t1);
Texture2D<float4> tCurFrameMap : register(t2);
Texture2D<float4> tDepthMap : register(t3);
Texture2D<float4> tOITAlphaSrc : register(t4);
Texture2D<float4> tOITDepthSrc : register(t5);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  //TODO: user if no DoF

  r0.x = tDepthMap.Sample(sDepthMapSampler_s, v1.xy).x;
  r0.x = -k_fFarDOFParams.x + r0.x;
  r0.x = saturate(k_fFarDOFParams.y * r0.x);
  r0.y = k_fFarDOFParams.z * r0.x;
  if (k_bOITFastVolumetricApprox != 0) {
    r0.z = tOITAlphaSrc.SampleLevel(sOITAlphaSrcSampler_s, v1.xy, 0).x;
    r0.w = cmp(r0.z < 1);
    if (r0.w != 0) {
      r0.w = tOITDepthSrc.SampleLevel(sOITDepthSrcSampler_s, v1.xy, 0).x;
      r0.w = -k_fFarDOFParams.x + r0.w;
      r0.w = saturate(k_fFarDOFParams.y * r0.w);
      r0.w = k_fFarDOFParams.z * r0.w;
      r0.x = r0.x * k_fFarDOFParams.z + -r0.w;
      r0.y = r0.z * r0.x + r0.w;
    }
  }
  r0.xzw = tCurFrameMap.Sample(sCurFrameMapSampler_s, v2.xy).xyz;
    // o0.xyz = r0.xzw; return; //debug

  r1.xyz = tCurFrameMap.Sample(sCurFrameMapSampler_s, v2.zw).xyz;
  r2.xyz = tCurFrameMap.Sample(sCurFrameMapSampler_s, v3.xy).xyz;
  r3.xyz = tCurFrameMap.Sample(sCurFrameMapSampler_s, v3.zw).xyz;
  r0.xzw = r1.xyz + r0.xzw;
  r0.xzw = r0.xzw + r2.xyz;
  r0.xzw = r0.xzw + r3.xyz;
  r0.xzw = float3(0.224303007,0.224303007,0.224303007) * r0.xzw;
  r1.xyz = tDownsampleMedium.Sample(sDownsampleMediumSampler_s, v1.xy).xyz;
    // o0.xyz = r1.xyz; return; //debug
  r2.xyzw = tDownsampleLarge.Sample(sDownsampleLargeSampler_s, v1.xy).xyzw;
    // o0.xyz = r2.xyz; return; //debug

  r1.w = k_fNearDOFParams.z * r2.w;
  r0.y = max(r1.w, r0.y);
  
  r3.xyzw = saturate(r0.yyyy * float4(-0.666666687,0.666666687,0.400000006,0.100000001) + float4(1,0,-0.600000024,-0.400000006));
  r3.yz = r3.yz + -r3.zw;
  o0.w = r3.y * 0.102787003 + r3.x;

  r1.xyz = r3.zzz * r1.xyz;
  r0.xyz = r0.xzw * r3.yyy + r1.xyz;
  o0.xyz = r2.xyz * r3.www + r0.xyz;

  o0 = max(o0, 0);
  o0.w = min(1, o0.w);
  return;
}