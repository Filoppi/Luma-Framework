#include "Includes/Common.hlsl"

cbuffer _Globals : register(b0)
{
  float ImageSpace_hlsl_BlurVertexShader00000000000000000000000000000004_3bits : packoffset(c0) = {0};
  float4 fogColor : packoffset(c1);
  float3 fogTransform : packoffset(c2);
  float2 fogLuminance : packoffset(c3);
  row_major float3x4 screenDataToCamera : packoffset(c4);
  float globalScale : packoffset(c7);
  float sceneDepthAlphaMask : packoffset(c7.y);
  float globalOpacity : packoffset(c7.z);
  float distortionBufferScale : packoffset(c7.w);
  float3 wToZScaleAndBias : packoffset(c8);
  float4 screenTransform[2] : packoffset(c9);
  float4 textureToPixel : packoffset(c11);
  float4 pixelToTexture : packoffset(c12);
  float maxScale : packoffset(c13) = {0};
  float bloomAlpha : packoffset(c13.y) = {0};
  float sceneBias : packoffset(c13.z) = {1};
  float3 gammaSettings : packoffset(c14);
  float exposure : packoffset(c14.w) = {0};
  float deltaExposure : packoffset(c15) = {0};
  float4 SampleOffsets[2] : packoffset(c16);
  float4 SampleWeights[4] : packoffset(c18);
  float4 PWLConstants : packoffset(c22);
  float PWLThreshold : packoffset(c23);
  float ShadowEdgeDetectThreshold : packoffset(c23.y);
  float4 ColorFill : packoffset(c24);
  float2 LowResTextureDimensions : packoffset(c25);
  float2 DownsizeTextureDimensions : packoffset(c25.z);
}

void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  float2 v2 : TEXCOORD1,
  out float4 o0 : TEXCOORD0,
  out float4 o1 : TEXCOORD1,
  out float4 o2 : SV_Position0)
{
  o0.xyzw = SampleOffsets[0].xyzw * LumaSettings.GameSettings.BloomRadius + v1.xyyx;
  o1.xyzw = SampleOffsets[1].xyzw * LumaSettings.GameSettings.BloomRadius + v1.xyyx;
  o2.xyzw = v0.xyzw;
}