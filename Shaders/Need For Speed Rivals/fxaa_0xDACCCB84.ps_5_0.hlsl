// ---- Created with 3Dmigoto v1.3.16 on Wed Jul 22 13:50:01 2026

cbuffer _Globals : register(b0)
{
  float2 rcpFrame : packoffset(c0);
  float4 rcpFrameOpt : packoffset(c1);
  float4 rcpFrameOpt2 : packoffset(c2);
  float4 fxaaXenonConstDir : packoffset(c3);
  float fxaaQualitySubpix : packoffset(c4) = {0.75};
  float fxaaQualityEdgeThreshold : packoffset(c4.y) = {0.165999994};
  float fxaaQualityEdgeThresholdMin : packoffset(c4.z) = {0.0833000019};
  float fxaaConsoleEdgeSharpness : packoffset(c4.w) = {8};
  float fxaaConsoleEdgeThreshold : packoffset(c5) = {0.25};
  float fxaaConsoleEdgeThresholdMin : packoffset(c5.y) = {0.0500000007};
}

SamplerState mainSampler_s : register(s0);
Texture2D<float4> mainTexture : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./common.hlsl"

#if TONEMAP_FXAA > 0
  #define FXAA_PC 1
  #define FXAA_HLSL_5 1
  #if TONEMAP_FXAA == 1
    #define FXAA_QUALITY__PRESET 12
  #elif TONEMAP_FXAA == 2
    #define FXAA_QUALITY__PRESET 39
  #endif
  #include "../Includes/FXAA.hlsl"
#endif

float3 DrawRect(float2 uv, float4 rect, float3 color, float3 rectColor) {
	float r = step(rect.x, uv.x) * step(uv.x, rect.z) * step(rect.y, uv.y) * step(uv.y, rect.w);
	if (r == 0) return color;
	return rectColor;
}

void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  #if TONEMAP_FXAA > 0
    FxaaTex fxaaTex = { mainSampler_s, mainTexture };

    o0.xyzw = FxaaPixelShader(
      v1.xy,                       // pos
      v2,                          // fxaaConsolePosPos
      fxaaTex,                     // tex
      fxaaTex,                     // fxaaConsole360TexExpBiasNegOne unused
      fxaaTex,                     // fxaaConsole360TexExpBiasNegTwo unused
      rcpFrame,                    // fxaaQualityRcpFrame
      rcpFrameOpt,                 // fxaaConsoleRcpFrameOpt
      rcpFrameOpt2,                // fxaaConsoleRcpFrameOpt2
      float4(0.0, 0.0, 0.0, 0.0),  // fxaaConsole360RcpFrameOpt2
      fxaaQualitySubpix,           // fxaaQualitySubpix
      fxaaQualityEdgeThreshold,    // fxaaQualityEdgeThreshold
      fxaaQualityEdgeThresholdMin, // fxaaQualityEdgeThresholdMin
      fxaaConsoleEdgeSharpness,    // fxaaConsoleEdgeSharpness
      fxaaConsoleEdgeThreshold,    // fxaaConsoleEdgeThreshold
      fxaaConsoleEdgeThresholdMin, // fxaaConsoleEdgeThresholdMin
      fxaaXenonConstDir            // fxaaConsole360ConstDir
    );
  #else
    o0.xyzw = mainTexture.Sample(mainSampler_s, v1.xy);
  #endif

  #if TEST_USER_PEAK_FXAA == 0
    o0.xyz = RenderIntermediatePass(o0.xyz);
  #else
    o0.xyz = 0;
	  o0.xyz = DrawRect(v1.xy, float4(0.35, 0.47, 0.65, 0.53),     o0.xyz, 100000.f);
	  o0.xyz = DrawRect(v1.xy, float4(0.365, 0.483, 0.448, 0.517), o0.xyz, PeakWhiteNits*2);
	  o0.xyz = DrawRect(v1.xy, float4(0.458, 0.483, 0.542, 0.517), o0.xyz, PeakWhiteNits);
	  o0.xyz = DrawRect(v1.xy, float4(0.552, 0.483, 0.635, 0.517), o0.xyz, PeakWhiteNits/2);
    o0.xyz /= UIPaperWhiteNits;
    o0.xyz = RenderIntermediatePass_Encode(o0.xyz);
  #endif

  return;
}