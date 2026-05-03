#include "../Includes/Common.hlsl"

cbuffer PostFxSMAA : register(b0)
{
  float4x4 CameraSpaceToPreviousProjectedSpace : packoffset(c0);
  float4 QuadParams : packoffset(c4);
  float4 SceneTextureSize : packoffset(c5);
  float4 TemporalParameters : packoffset(c6);
}

SamplerState ColorPointClamp2D_s : register(s0);
Texture2D<float4> PostFxSMAA__ColorTexture__TexObj__ : register(t0);


// 3Dmigoto declarations
#define cmp -


void main(
  linear centroid float2 v0 : TEXCOORD0,
  float4 v1 : SV_Position0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = SceneTextureSize.zwzw * float4(-2,0,0,-2) + v0.xyxy;
  r1.xyz = PostFxSMAA__ColorTexture__TexObj__.Sample(ColorPointClamp2D_s, r0.xy).xyz;
  r0.xyz = PostFxSMAA__ColorTexture__TexObj__.Sample(ColorPointClamp2D_s, r0.zw).xyz;
  r0.y = dot(r0.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.x = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.xyzw = SceneTextureSize.zwzw * float4(-1,0,0,-1) + v0.xyxy;
  r2.xyz = PostFxSMAA__ColorTexture__TexObj__.Sample(ColorPointClamp2D_s, r1.xy).xyz;
  r1.xyz = PostFxSMAA__ColorTexture__TexObj__.Sample(ColorPointClamp2D_s, r1.zw).xyz;
  r1.y = dot(r1.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.x = dot(r2.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.xy = r1.xy + -r0.xy;
  r2.xyz = PostFxSMAA__ColorTexture__TexObj__.Sample(ColorPointClamp2D_s, v0.xy).xyz;
  r0.z = dot(r2.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r1.xy = r0.zz + -r1.xy;
  r2.xyzw = SceneTextureSize.zwzw * float4(1,0,0,1) + v0.xyxy;
  r3.xyz = PostFxSMAA__ColorTexture__TexObj__.Sample(ColorPointClamp2D_s, r2.xy).xyz;
  r2.xyz = PostFxSMAA__ColorTexture__TexObj__.Sample(ColorPointClamp2D_s, r2.zw).xyz;
  r2.y = dot(r2.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r2.x = dot(r3.xyz, float3(0.212599993,0.715200007,0.0722000003));
  r0.zw = -r2.xy + r0.zz;
  r0.zw = max(abs(r1.xy), abs(r0.zw));
  r0.xy = max(r0.zw, abs(r0.xy));
  r0.x = max(r0.x, r0.y);
  r0.yz = abs(r1.xy) + abs(r1.xy);
  r1.xy = cmp(abs(r1.xy) >= float2(0.100000001,0.100000001));
  r1.xy = r1.xy ? float2(1,1) : 0;
  r0.xy = cmp(r0.yz >= r0.xx);
  r0.xy = r0.xy ? float2(1,1) : 0;
  o0.xy = float2(0,0);//r1.xy * r0.xy;
  
  if (LumaSettings.SRType != 0)
  {
	o0.xy = float2(0,0);//r1.xy * r0.xy;
  }
  else
  {
    o0.xy = r1.xy * r0.xy;
  }
  
  o0.zw = float2(0,0);
  return;
}