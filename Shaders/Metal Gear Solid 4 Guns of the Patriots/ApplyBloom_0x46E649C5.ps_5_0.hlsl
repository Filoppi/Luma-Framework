#include "../Includes/Common.hlsl"

#ifndef ENABLE_IMPROVED_BLUR
#define ENABLE_IMPROVED_BLUR 1
#endif

#ifndef ENABLE_BLOOM
#define ENABLE_BLOOM 1
#endif

Texture2D<float4> t6 : register(t6);
Texture2D<float4> t5 : register(t5);
Texture2D<float4> t4 : register(t4);
Texture2D<float4> t3 : register(t3);
Texture2D<float4> t2 : register(t2);
Texture2D<float4> t1 : register(t1);
Texture2D<float4> t0 : register(t0);

// All clamp linear samplers
SamplerState s6_s : register(s6);
SamplerState s5_s : register(s5);
SamplerState s4_s : register(s4);
SamplerState s3_s : register(s3);
SamplerState s2_s : register(s2);
SamplerState s1_s : register(s1);
SamplerState s0_s : register(s0);

// TODO1: fix all bloom stretching in UW
void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float2 v2 : TEXCOORD0,
  out float4 o0 : SV_TARGET0)
{
#if !ENABLE_BLOOM
  o0 = float4(0.0, 0.0, 0.0, 1.0);
  return;
#endif

  float4 r0,r1;
  r0.xyzw = t1.Sample(s1_s, v2.xy).xyzw;
  r0.xyz = r0.xyz / r0.www;
  r0.xyz = float3(0.020833334,0.020833334,0.020833334) * r0.xyz;
#if ENABLE_IMPROVED_BLUR
  uint sourceWidth, sourceHeight;
  t0.GetDimensions(sourceWidth, sourceHeight);
  float2 uvDx = ddx(v2.xy);
  float2 uvDy = ddy(v2.xy);
  // Fix bloom upscaling not reading all samples in this specific case (which might be all the cases)
  float2 outputResolution = round(rcp(abs(uvDx) + abs(uvDy)));
  [branch]
  if (sourceWidth == 256 && sourceHeight == 256 && all(outputResolution == 1024.0)) // TODO1: invalid now. We scaled it.
  {
    float2 uvMin = v2.xy - (0.5 / 256.0);
    float2 uvMax = v2.xy + (0.5 / 256.0);
    r1 = t0.SampleLevel(s0_s, uvMin, 0);
    r1 += t0.SampleLevel(s0_s, float2(uvMax.x, uvMin.y), 0);
    r1 += t0.SampleLevel(s0_s, float2(uvMin.x, uvMax.y), 0);
    r1 += t0.SampleLevel(s0_s, uvMax, 0);
    r1 *= 0.25;
  }
  else
    r1 = t0.SampleGrad(s0_s, v2.xy, uvDx, uvDy);
#else
  r1.xyzw = t0.Sample(s0_s, v2.xy).xyzw;
#endif
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.020833334,0.020833334,0.020833334) + r0.xyz;
  r1.xyzw = t2.Sample(s2_s, v2.xy).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.027777778,0.027777778,0.027777778) + r0.xyz;
  r1.xyzw = t3.Sample(s3_s, v2.xy).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.0347222239,0.0347222239,0.0347222239) + r0.xyz;
  r1.xyzw = t4.Sample(s4_s, v2.xy).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.0416666679,0.0416666679,0.0416666679) + r0.xyz;
  r1.xyzw = t5.Sample(s5_s, v2.xy).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.0486111119,0.0486111119,0.0486111119) + r0.xyz;
  r1.xyzw = t6.Sample(s6_s, v2.xy).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r0.xyz = r1.xyz * float3(0.055555556,0.055555556,0.055555556) + r0.xyz;
  o0.xyz = v1.xyz * r0.xyz;
  o0.w = 1;
}
