#include "Includes/Common.hlsl"

// rl_distortion_02: screen distortion with the "signal noise" TV look (RGB split, per-line sine wobble, a fixed colour
// shift). Transcribed from the listing. Vanilla saturates the colour-shifted value, which clips the HDR scene during
// the effect; in HDR only negatives are removed, SDR keeps the vanilla clip.

cbuffer CB_MATERIAL : register(b0)
{
   float4 cb0[3];
}
cbuffer CB_PIXEL : register(b3)
{
   float4 cb3[11];
}

#define BlurScale       cb0[0].x
#define DistortionScale cb0[1].xy
#define SignalNoise     cb0[2].xyz
#define TintColor       cb3[10]

Texture2D<float4> BaseTexture : register(t0);
Texture2D<float4> DistortionTexture : register(t1);
Texture2D<float4> BlurredTexture : register(t2);
SamplerState Sampler_Linear_CC : register(s2);

float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD0) : SV_Target
{
   const float4 distortion = DistortionTexture.Sample(Sampler_Linear_CC, uv);
   const float signalMask = 1.0 - distortion.w * TintColor.w;
   const float3 tinted = distortion.xyz * TintColor.xyz;
   const float wobble = SignalNoise.y * sin((uv.y + SignalNoise.z) * SignalNoise.x * 3.141593) + signalMask;
   const float blurAmount = min(tinted.z * BlurScale, 1.0);

   const float2 offset = (floor(tinted.xy * 256.0) * 0.0078125 - 1.0) * DistortionScale;
   const float2 splitStep = offset + 0.0005;
   const float2 distortedUV = offset + uv;

   const float4 base = BaseTexture.Sample(Sampler_Linear_CC, distortedUV);
   const float blue = BaseTexture.Sample(Sampler_Linear_CC, splitStep * wobble + distortedUV).z;
   const float green = BaseTexture.Sample(Sampler_Linear_CC, float2(splitStep.x * wobble * 0.5 + distortedUV.x, signalMask * splitStep.y * 0.5 + distortedUV.y)).y;
   const float3 blurred = BlurredTexture.Sample(Sampler_Linear_CC, distortedUV).rgb;

   const float3 split = float3(base.x, green, blue);
   const float ratio = saturate(base.x / green);
   const float2 shift = min(ratio * float2(2.5, 3.333333), 1.0);
   float3 tv = float3(split.x, split.y - 0.2 + shift.x * 0.2, split.z) * (shift.y * float3(0.1, 0.25, 0.0) + float3(0.9, 0.75, 1.0));
   tv = SRTTR_HDR_SCENE ? max(0.0, tv) : saturate(tv);

   const float3 color = signalMask * (tv - split) + split;
   return float4(blurAmount * (blurred - color) + color, base.w);
}
