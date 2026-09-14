
cbuffer _cb0 : register(b0)
{
  float4 c_eye_forward : packoffset(c0);
  float4 c_view_perpendicular_color : packoffset(c1);
  float4 c_view_parallel_color : packoffset(c2);
}

SamplerState TexS0_s : register(s0);
SamplerState TexS3_s : register(s3);
Texture2D<float4> Texture0 : register(t0);
TextureCube<float4> Texture3 : register(t3);


// 3Dmigoto declarations
#define cmp -


void main(
  float4 v0 : SV_POSITION0,
  float4 v1 : COLOR0,
  float4 v2 : COLOR1,
  float4 v3 : TEXCOORD0,
  float4 v4 : TEXCOORD1,
  float4 v5 : TEXCOORD2,
  float4 v6 : TEXCOORD3,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  uint4 bitmask, uiDest;
  float4 fDest;

  r0.xyz = Texture0.Sample(TexS0_s, v3.xy).xyz;
    // r0.xyz = saturate(r0.xyz);
  r0.xyz = r0.xyz * float3(2,2,2) + float3(-1,-1,-1);
  r1.x = dot(v4.xyz, r0.xyz);
  r1.y = dot(v5.xyz, r0.xyz);
  r1.z = dot(v6.xyz, r0.xyz);
  r0.x = dot(r1.xyz, r1.xyz);
  r0.x = rsqrt(r0.x);
  r0.xyz = r1.xyz * r0.xxx;
  r1.x = v4.w;
  r1.y = v5.w;
  r1.z = v6.w;
  r0.w = dot(r1.xyz, r1.xyz);
  r0.w = rsqrt(r0.w);
  r1.xyz = r1.xyz * r0.www;
  r0.w = dot(r0.xyz, r1.xyz);
  r1.w = r0.w + r0.w;
  r0.w = r0.w * r0.w;
  r0.xyz = r1.www * r0.xyz + -r1.xyz;
  r1.x = dot(r0.xyz, r0.xyz);
  r1.x = rsqrt(r1.x);
  r0.xyz = r1.xxx * r0.xyz;
  r0.xyz = Texture3.Sample(TexS3_s, r0.xyz).xyz;
    // r0.xyz = saturate(r0.xyz);
  r1.xyz = r0.xyz * r0.xyz;
  r1.xyz = r1.xyz * r1.xyz;
  r0.xyz = -r1.xyz * r1.xyz + r0.xyz;
  r1.xyz = r1.xyz * r1.xyz;
  r2.xyzw = -c_view_parallel_color.wxyz + c_view_perpendicular_color.wxyz;
  r2.xyzw = r0.wwww * r2.xyzw + c_view_parallel_color.wxyz;
  r0.xyz = r2.yzw * r0.xyz + r1.xyz;
  o0.xyz = r0.xyz * r2.xxx;
  o0.xyz = max(o0.xyz, 0);
  o0.w = 0;
  return;
}