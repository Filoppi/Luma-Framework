#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float ImageSpace_hlsl_BlurPixelShader048_6bits : packoffset(c0) = {0};
  float4 fogColor : packoffset(c1);
  float3 fogTransform : packoffset(c2);
  float4x3 screenDataToCamera : packoffset(c3);
  float globalScale : packoffset(c6);
  float sceneDepthAlphaMask : packoffset(c6.y);
  float globalOpacity : packoffset(c6.z);
  float distortionBufferScale : packoffset(c6.w);
  float2 wToZScaleAndBias : packoffset(c7);
  float4 screenTransform[2] : packoffset(c8);
  float4 textureToPixel : packoffset(c10);
  float4 pixelToTexture : packoffset(c11);
  float maxScale : packoffset(c12) = {0};
  float bloomAlpha : packoffset(c12.y) = {0};
  float sceneBias : packoffset(c12.z) = {1};
  float exposure : packoffset(c12.w) = {0};
  float deltaExposure : packoffset(c13) = {0};
  float4 SampleOffsets[8] : packoffset(c14);
  float4 SampleWeights[16] : packoffset(c22);
  float4 PWLConstants : packoffset(c38);
  float PWLThreshold : packoffset(c39);
  float ShadowEdgeDetectThreshold : packoffset(c39.y);
  float4 ColorFill : packoffset(c40);
}

SamplerState s_buffer_s : register(s0);
Texture2D<float4> s_buffer : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : TEXCOORD0,
  float4 v1 : TEXCOORD1,
  float4 v2 : TEXCOORD2,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  float4 v5 : TEXCOORD5,
  float4 v6 : TEXCOORD6,
  float4 v7 : TEXCOORD7,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  float4 v[8] = { v0,v1,v2,v3,v4,v5,v6,v7 };
  r1.x = 0;
  r0.xyzw = float4(0,0,0,0);
  while (true) {
    r1.y = cmp((int)r1.x >= 15);
    if (r1.y != 0) break;
    r1.y = (uint)r1.x >> 1;
    r2.xyzw = s_buffer.Sample(s_buffer_s, v[r1.y+0].xy).xyzw;
    r2.xyzw = r2.xyzw * SampleWeights[r1.x].xyzw + r0.xyzw;
    r3.xyzw = s_buffer.Sample(s_buffer_s, v[r1.y+0].wz).xyzw;
    r1.y = (int)r1.x + 1;
    r0.xyzw = r3.xyzw * SampleWeights[r1.y].xyzw + r2.xyzw;
    r1.x = (int)r1.x + 2;
  }
  o0.w = r0.w;
  r0.w = dot(r0.xyz, float3(0.270000011,0.670000017,0.0599999987));
  r0.w = max(9.99999975e-006, r0.w);
  r1.x = cmp(r0.w < PWLThreshold);

#if ENABLE_IMPROVED_BLOOM
  // Make the curve under the threshold non linear.
  r1.y = pow(r0.w, 2.0) * PWLConstants.x + PWLConstants.y;
  r1.z = r0.w * PWLConstants.z + PWLConstants.w;
#else
  // Original curves.
  r1.yz = r0.ww * PWLConstants.xz + PWLConstants.yw;
#endif

  r1.x = r1.x ? r1.y : r1.z;
  r0.w = r1.x / r0.w;
  o0.xyz = r0.xyz * r0.www;
  return;
}