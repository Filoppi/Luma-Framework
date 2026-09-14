// ---- Created with 3Dmigoto v1.3.16 on Tue Aug 04 12:24:15 2026

SamplerState PS_SAMPLERS_3__s : register(s3);
SamplerState PS_SAMPLERS_4__s : register(s4);
Texture2D<float4> PS_TEXTURES_2D_0_ : register(t0); //normals
Texture2D<float4> PS_TEXTURES_2D_2_ : register(t2); //depth


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_Position0,
  float2 v1 : TEXCOORD0,
  float2 w1 : TEXCOORD1,
  float2 v2 : TEXCOORD2,
  float2 w2 : TEXCOORD3,
  out float4 o0 : SV_Target0,
  out float oDepth : SV_Depth)
{
  float4 r0,r1,r2,r3;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.yz = v2.xy;
  r1.yz = w1.xy;
  r2.yz = v1.xy;

  r3.xyzw = PS_TEXTURES_2D_2_.Gather(PS_SAMPLERS_3__s, float2(v1.x, v1.y)).wxyz; //depth
  r0.w = cmp(r3.z < r3.y);
  r1.x = r3.z;
  r2.x = r3.y;
  r1.xyz = r0.www ? r1.xyz : r2.xyz;
  r0.w = cmp(r3.w < r1.x);
  r0.x = r3.w;
  r0.xyz = r0.www ? r0.xyz : r1.xyz;
  r0.w = cmp(r3.x < r0.x);
  r3.yz = w2.xy;
  r0.xyz = r0.www ? r3.xyz : r0.xyz;

  o0.xyzw = PS_TEXTURES_2D_0_.Sample(PS_SAMPLERS_4__s, r0.yz).xyzw; //normals
  
  oDepth = r0.x;
  return;
}