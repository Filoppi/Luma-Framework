#include "Includes/Common.hlsl"

#define _RT_SAMPLE1 1
#define _RT_SCENE_SELECTION 1
#define _RT_NEAREST 0

cbuffer PER_BATCH : register(b0)
{
  float4 VisionMtlParams : packoffset(c0);
}

cbuffer PER_INSTANCE : register(b1)
{
  float4 SceneSelection : packoffset(c0);
}

#include "Includes/CBuffer_PerViewGlobal.hlsl"

SamplerState PNoiseSampler_s : register(s1);
Texture2D<float4> PNoiseSampler : register(t1);
Texture2D<float4> sceneMaskDeviceTex : register(t26);

#define cmp -

// This draws some objects highlights directly on the back buffer, after tonemapping but before AA (the output alpha is ignored, it's adding the color anyway).
// These look best linearized by channel at the end, even if it's additive and thus the concept of gamma on additive colors is a bit fuzzy "(linear+linear) != toLinear(gamma+gamma)" (there's no way to emulate the SDR gamma space additive blends look).
void main(
  float4 v0 : SV_Position0,
  float4 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD2,
  nointerpolation float4 cVision : TEXCOORD3,
  uint v5 : SV_IsFrontFace0,
  out float4 o0 : SV_Target0)
{
  v0.xy *= float2(BaseHorizontalResolution, BaseVerticalResolution) / CV_ScreenSize.xy;
	v0.xy += LumaData.GameData.CameraJitters.xy * float2(0.5, -0.5) * CV_ScreenSize.xy * LumaData.RenderResolutionScale;

  float4 r0,r1;

  r0.xy = (int2)v0.xy;
  r0.zw = float2(0,0);
  r0.x = sceneMaskDeviceTex.Load(r0.xyz).x;
  r0.x = v0.z + -r0.x;
  r0.y = CV_LookingGlass_DepthScalar * SceneSelection.x;
  r0.x = r0.y * r0.x;
  r0.x = cmp(r0.x < 0);
  if (r0.x != 0) discard;
  r0.x = dot(-v2.xyz, -v2.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = -v2.xyz * r0.xxx;
  r0.w = v5.x ? 1 : -1;
  r1.xyz = v3.xyz * r0.www;
  r0.x = saturate(dot(r0.xyz, r1.xyz));
  r0.x = 1 + -r0.x;
  r0.x = max(0.5, r0.x);
  r0.y = 0.349999994 * v0.y;
  r0.y = frac(r0.y);
  r0.y = r0.y * 2 + -1;
  r0.y = abs(r0.y) * 0.5 + 0.5;
  r0.yzw = cVision.xyz * r0.yyy;
  r0.xyz = r0.yzw * r0.xxx;
  r0.w = PNoiseSampler.Sample(PNoiseSampler_s, v1.xy).x;
  r0.w = VisionMtlParams.z * VisionMtlParams.x + r0.w;
  r0.w = frac(r0.w);
  r0.w = -0.5 + r0.w;
  r0.xyz = r0.xyz * abs(r0.www);
  r0.xyz = VisionMtlParams.yyy * r0.xyz;
  r0.w = VisionMtlParams.z / VisionMtlParams.w;
  r0.w = 3.1415925 * r0.w;
  r0.w = sin(r0.w);
  r0.w = saturate(1.20000005 * r0.w);
  o0.xyz = r0.xyz * r0.www; // Pre-multiplied alpha (it doesn't seem to do anything as alpha is 1)
  o0.w = 1;
  // We could call "ConditionalLinearizeUI()", though "LumaUIData.AlphaBlendState" here seems to be 1.
  o0 = SDRToHDR(o0);
#if POST_PROCESS_SPACE_TYPE >= 1
#if 1
  o0.rgb *= 1.5f; // Empirically found multiplier to align the HDR (linear blend) color to the SDR (gamma blend) one
#else // Doesn't look good
  static const float BackgroundMidGray = 0.333; // In gamma space. Anything between 0.125 and 0.5 could work.
  o0.rgb *= (BackgroundMidGray + BackgroundMidGray) / pow(pow(BackgroundMidGray, DefaultGamma) * pow(BackgroundMidGray, DefaultGamma), 1.f / DefaultGamma);
#endif
#endif // POST_PROCESS_SPACE_TYPE >= 1
#if !ENABLE_ARK_CUSTOM_POST_PROCESS || !ENABLE_OBJECT_HIGHLIGHTING
  o0 = 0;
#endif
}