// ---- Created with 3Dmigoto v1.4.1 on Fri Mar 20 17:25:30 2026

cbuffer ParamBuffer : register(b1)
{
  float4 materialColor_ : packoffset(c0);
  float4x4 coordMatrix_ : packoffset(c1);
}



// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : POSITION0,
  float2 v1 : TEXCOORD0,
  out float4 o0 : SV_Position0,
  out float2 o1 : TEXCOORD0)
{
  float4 r0;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyzw = v0.xyzw * float4(2,2,1,1) + float4(1,1,0,0);
  o0.x = dot(r0.xyzw, coordMatrix_._m00_m10_m20_m30);
  o0.y = dot(r0.xyzw, coordMatrix_._m01_m11_m21_m31);
  o0.z = dot(r0.xyzw, coordMatrix_._m02_m12_m22_m32);
  o0.w = dot(r0.xyzw, coordMatrix_._m03_m13_m23_m33);
  o1.xy = v1.xy;
  return;
}