cbuffer cb0 : register(b0)
{
  float4 cb0[9];
}

void main(
  float4 v0 : COLOR0,
  int4 v1 : POSITION0,
  int2 v2 : TEXCOORD0,
  float4 v3 : TEXCOORD1,
  float4 v4 : TEXCOORD2,
  float4 v5 : TEXCOORD4,
  float4 v6 : TEXCOORD5,
  float4 v7 : TEXCOORD7,
  out float4 o0 : SV_POSITION0,
  out float4 o1 : COLOR0,
  out float2 o2 : TEXCOORD0,
  out float4 o3 : TEXCOORD1)
{
  float4 r0;
  r0.xy = (float2)v1.xy;
  r0.xy = v3.xy * r0.xy;
  r0.yzw = v6.xyz * r0.yyy;
  r0.xyz = r0.xxx * v5.xyz + r0.yzw;
  r0.xyz = v7.xyz + r0.xyz;
  o0.xyz = r0.xyz;
  o0.w = 1;
  o1.xyzw = v4.xyzw * v0.wzyx;
  r0.zw = (float2)v2.xy;
  r0.zw = v3.zw * r0.zw;
  o2.xy = r0.zw * cb0[8].xy + cb0[8].zw;
  // Luma: the game scales the vignette Y by 1.5 / aspect ratio (cb0[6].z is 0.84375 at 16:9 and 0.421875 at 32:9), so at ultrawide the vertical radius shrinks and the vignette gets much weaker.
  // Use the 16:9 value (at least) so edges and corners darken the same as 16:9, while still stretching across the full width.
  static const float nativeVignetteYScale = 1.5 * 9.0 / 16.0;
  o3.y = max(cb0[6].z, nativeVignetteYScale) * r0.y;
  o3.x = r0.x;
  o3.zw = 0; // Og only wrote z but we also write w to avoid warnings...
}