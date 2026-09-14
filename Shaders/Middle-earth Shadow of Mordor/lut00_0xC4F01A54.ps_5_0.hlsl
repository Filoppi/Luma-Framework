// ---- Created with 3Dmigoto v1.3.16 on Sun Jul 12 21:43:56 2026

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

  float2 k_fLUT1ScaleAndOffset : packoffset(c5) = {0.875,0.0625};
  float2 k_fLUT2ScaleAndOffset : packoffset(c5.z) = {0.875,0.0625};
  float k_fLUTBlend : packoffset(c6) = {0};
  float2 k_vScene_TexCoordScale : packoffset(c6.y);
}

SamplerState sColorGrade3DLUT1Sampler_s : register(s0);
SamplerState sColorGrade3DLUT2Sampler_s : register(s1);
SamplerState sCurFrameMapDiscardSampler_s : register(s2);
Texture3D<float4> tColorGrade3DLUT1 : register(t0);
Texture3D<float4> tColorGrade3DLUT2 : register(t1);
Texture2D<float4> tCurFrameMapDiscard : register(t2);


// 3Dmigoto declarations
#define cmp -
#include "./common.hlsl"

#define LUT_3D 1
#include "../Includes/ColorGradingLUT.hlsl"

void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = tCurFrameMapDiscard.Sample(sCurFrameMapDiscardSampler_s, v1.xy).xyz;
  r0.xyz = max(0, r0.xyz); //clean

  // Gamma Decode
  r0.xyz = sRGB_Decode(r0.xyz);

  // new rolloff
  const float peak = GammaCorrectionPeak(HDR_PEAK);
  r0.xyz = ReinhardPiecewise(r0.xyz, peak, 0.260396);

  if (true /* SI.lut */) {
    // colorU / Original
    float3 colorU = r0.xyz;

    // colorN / Compress
    {
      float y = GetLuminance(r0.xyz, CS_BT709);
      float y1 = y;
      y1 = Neupow(y1, 0.96, peak, 1.);
      r0.xyz = r0.xyz * DivideSafe(y1, y, 0);
      // r0.xyz = ClampByMaxChannel(r0.xyz, 1);
    }
    float3 colorN = r0.xyz;

    // colorT / Sample
    r0.xyz = sRGB_Encode(r0.xyz);
    {
      float3 c0 = r0.xyz/*  * k_fLUT2ScaleAndOffset.xxx + k_fLUT2ScaleAndOffset.yyy */;
      float3 c1 = r0.xyz/*  * k_fLUT1ScaleAndOffset.xxx + k_fLUT1ScaleAndOffset.yyy */;
      // c0 = tColorGrade3DLUT1.SampleLevel(sColorGrade3DLUT1Sampler_s, c0, 0).xyz;
      // c1 = tColorGrade3DLUT2.SampleLevel(sColorGrade3DLUT2Sampler_s, c1, 0).xyz;
      c0 = SampleLUT(tColorGrade3DLUT1, sColorGrade3DLUT1Sampler_s, c0, 8, true);
      c1 = SampleLUT(tColorGrade3DLUT2, sColorGrade3DLUT2Sampler_s, c1, 8, true);
      r0.xyz = lerp(c0, c1, k_fLUTBlend);
    }
    r0.xyz = sRGB_Decode(r0.xyz);
    float3 colorT = r0.xyz;

    // Decompress
    r0.xyz = RestorePostProcess(colorU, colorN, colorT);
  }

  // RenderIntermediatePass
  r0.xyz = min(r0.xyz, peak);
  r0.xyz = RenderIntermediatePassFromLinear(r0.xyz);

  o0.xyz = r0.xyz;
  o0.w = 1;
  return;
}