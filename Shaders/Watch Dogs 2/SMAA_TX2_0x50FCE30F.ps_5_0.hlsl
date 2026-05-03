#include "../Includes/Common.hlsl"

cbuffer PostFxSMAA : register(b0)
{
  float4x4 CameraSpaceToPreviousProjectedSpace : packoffset(c0);
  float4 QuadParams : packoffset(c4);
  float4 SceneTextureSize : packoffset(c5);
  float4 TemporalParameters : packoffset(c6);
}

SamplerState ColorClamp_s : register(s0);
SamplerState ColorPointClamp_s : register(s1);
Texture2D<float4> PostFxSMAA__ColorTexture__TexObj__ : register(t0);
Texture2D<float2> PostFxSMAA__FullMotionVectorsHistoryTexture__TexObj__ : register(t1);
Texture2D<float2> PostFxSMAA__FullMotionVectorsTexture__TexObj__ : register(t2);
Texture2D<float4> PostFxSMAA__SceneColorAccumulationTexture__TexObj__ : register(t3);
Texture2D<float4> PostFxSMAA__SceneColorPrevPrevFrameTexture__TexObj__ : register(t4);
Texture2D<float4> PostFxSMAA__SceneColorPreviousFrameTexture__TexObj__ : register(t5);

void main(
  linear centroid float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3,r4,r5,r6;
  r0.xyz = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorPointClamp_s, v0.xy, 0, int2(-1, 0)).xyz;
  r1.xyz = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorPointClamp_s, v0.xy, 0, int2(0, 1)).xyz;
  r2.xyz = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorPointClamp_s, v0.xy, 0, int2(1, 1)).xyz;
  r3.xyz = max(r2.xyz, r1.xyz);
  r1.xyz = min(r2.xyz, r1.xyz);
  r2.xyz = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorPointClamp_s, v0.xy, 0, int2(-1, 1)).xyz;
  r3.xyz = max(r2.xyz, r3.xyz);
  r1.xyz = min(r2.xyz, r1.xyz);
  r2.xyz = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorPointClamp_s, v0.xy, 0, int2(1, 0)).xyz;
  r3.xyz = max(r2.xyz, r3.xyz);
  r1.xyz = min(r2.xyz, r1.xyz);
  r2.xyz = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorPointClamp_s, v0.xy, 0, int2(0, 0)).xyz;
  r3.xyz = max(r2.xyz, r3.xyz);
  r3.xyz = max(r3.xyz, r0.xyz);
  r4.xyz = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorPointClamp_s, v0.xy, 0, int2(1, -1)).xyz;
  r3.xyz = max(r4.xyz, r3.xyz);
  r5.xyz = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorPointClamp_s, v0.xy, 0, int2(0, -1)).xyz;
  r3.xyz = max(r5.xyz, r3.xyz);
  r6.xyz = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorPointClamp_s, v0.xy, 0, int2(-1, -1)).xyz;
  r3.xyz = max(r6.xyz, r3.xyz);
  r1.xyz = min(r2.xyz, r1.xyz);
  r0.xyz = min(r1.xyz, r0.xyz);
  r0.xyz = min(r4.xyz, r0.xyz);
  r0.xyz = min(r5.xyz, r0.xyz);
  r0.xyz = min(r6.xyz, r0.xyz);
  r1.xy = PostFxSMAA__FullMotionVectorsTexture__TexObj__.Load(int3(v1.xy, 0)).xy;
  r1.zw = -r1.xy * SceneTextureSize.zw + v0.xy;
  r4.xyz = PostFxSMAA__SceneColorPreviousFrameTexture__TexObj__.SampleLevel(ColorClamp_s, r1.zw, 0).xyz;
  r5.xyz = max(r4.xyz, r0.xyz);
  r5.xyz = min(r5.xyz, r3.xyz);
  r5.xyz = r5.xyz * r5.xyz;
  r4.xyz = r4.xyz * r4.xyz + -r5.xyz;
  r6.xy = PostFxSMAA__FullMotionVectorsHistoryTexture__TexObj__.SampleLevel(ColorClamp_s, r1.zw, 0).xy;
  r6.zw = r6.xy + r1.xy;
  r1.xy = r6.xy + -r1.xy;
  r0.w = dot(r1.xy, r1.xy);
  r0.w = sqrt(r0.w);
  r0.w = 0.5 * r0.w;
  r0.w = min(1, r0.w);
  r0.w = 1 + -r0.w;
  r0.w = 0.5 * r0.w;
  r1.xy = -r6.zw * SceneTextureSize.zw + v0.xy;
  r6.xyz = PostFxSMAA__SceneColorPrevPrevFrameTexture__TexObj__.SampleLevel(ColorClamp_s, r1.xy, 0).xyz;
  r6.xyz = -r6.xyz + r2.xyz;
  r1.x = max(abs(r6.y), abs(r6.z));
  r1.x = max(abs(r6.x), r1.x);
  r1.x = r1.x * 32 + 1;
  r1.x = 1 / r1.x;
  r4.xyz = r1.x * r4.xyz + r5.xyz;
  r4.xyz = -r2.xyz * r2.xyz + r4.xyz;
  r1.xy = (float2(1,1) < r1.zw);
  r5.xy = (r1.zw < float2(0,0));
  r6.xyz = PostFxSMAA__SceneColorAccumulationTexture__TexObj__.SampleLevel(ColorClamp_s, r1.zw, 0).xyz;
  r1.x = asfloat(asint(r1.x) | asint(r5.x));
  r1.x = asfloat(asint(r1.y) | asint(r1.x));
  r1.x = asfloat(asint(r5.y) | asint(r1.x));
  r0.w = r1.x ? 0 : r0.w;
  r1.yzw = r2.xyz * r2.xyz;
  r2.xyz = -r6.xyz + r2.xyz;
  r2.x = dot(abs(r2.xyz), abs(r2.xyz));
  r2.x = sqrt(r2.x);
  r1.yzw = r0.w * r4.xyz + r1.yzw;
  r2.yzw = max(r6.xyz, r0.xyz);
  r2.yzw = min(r3.xyz, r2.yzw);
  r1.yzw = -r2.yzw * r2.yzw + r1.yzw;
  r2.yzw = r2.yzw * r2.yzw;
  r4.xyz = r3.xyz + -r0.xyz;
  r0.xyz = -TemporalParameters.x * r4.xyz + r0.xyz;
  r3.xyz = TemporalParameters.x * r4.xyz + r3.xyz;
  r3.xyz = saturate(r6.xyz + -r3.xyz);
  r0.xyz = max(float3(0,0,0), r0.xyz);
  r0.xyz = saturate(r0.xyz + -r6.xyz);
  r0.xyz = r0.xyz + r3.xyz;
  r0.x = dot(r0.xyz, r0.xyz);
  r0.x = sqrt(r0.x);
  r0.x = TemporalParameters.w * r0.x;
  r0.x = TemporalParameters.z * r2.x + -r0.x;
  r0.x = max(0, r0.x);
  r0.x = 0.001 + r0.x;
  r0.x = 1 / r0.x;
  r0.x = min(1, r0.x);
  r0.x = max(TemporalParameters.y, r0.x);
  r0.x = r1.x ? 1 : r0.x;
  r0.xyz = r0.x * r1.yzw + r2.yzw;
  if (LumaSettings.SRType != 0)
  {
	o0.xyz = PostFxSMAA__ColorTexture__TexObj__.SampleLevel(ColorPointClamp_s, v0.xy, 0).xyz;//sqrt(r0.xyz);
  }
  else
  {
    o0.xyz = sqrt(r0.xyz);
  }
  o0.w = 1;
}