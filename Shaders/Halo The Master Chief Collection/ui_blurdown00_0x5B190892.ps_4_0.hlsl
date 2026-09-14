// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 12:24:14 2026

cbuffer cbGaussianBlur : register(b0)
{

  struct
  {
    float4 Direction;
  } cbGaussianBlur : packoffset(c0);

}

SamplerState _texDiffuse_s : register(s0);
Texture2D<float4> texDiffuse : register(t0);


// 3Dmigoto declarations
#define cmp -
#include "./Includes/Common.hlsl"

void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;
  o0.w = 1;

  r0.xyzw = cbGaussianBlur.Direction.xyxy * float4(-0.5,0.5,-0.5,-0.5) + v1.xyxy;
  r1.xyzw = texDiffuse.SampleLevel(_texDiffuse_s, r0.xy, cbGaussianBlur.Direction.w).xyzw;
  r0.xyzw = texDiffuse.SampleLevel(_texDiffuse_s, r0.zw, cbGaussianBlur.Direction.w).xyzw;
  r0.xyz = r1.xyz + r0.xyz;
  
  r1.xyzw = cbGaussianBlur.Direction.xyxy * float4(0.5,0.5,0.5,-0.5) + v1.xyxy;
  r2.xyzw = texDiffuse.SampleLevel(_texDiffuse_s, r1.xy, cbGaussianBlur.Direction.w).xyzw;
  r1.xyzw = texDiffuse.SampleLevel(_texDiffuse_s, r1.zw, cbGaussianBlur.Direction.w).xyzw;
  r0.xyz = r2.xyz + r0.xyz;
  r0.xyz = r0.xyz + r1.xyz;
  o0.xyz = float3(0.25,0.25,0.25) * r0.xyz;
  if ((IsGame_Halo3() || IsGame_Halo3ODST()) && GS.UIBlurDown0Count == 1) o0.xyz = sRGB_Encode(o0.xyz);
  
  return;
}