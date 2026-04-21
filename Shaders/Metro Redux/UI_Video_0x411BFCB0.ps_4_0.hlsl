#include "../Includes/Common.hlsl"

SamplerState s_image_s : register(s0);
Texture2D<float4> t_image : register(t0);

void main(
  float4 v0 : SV_Position0,
  float3 v1 : TEXCOORD0,
  float w1 : TEXCOORD2,
  float4 v2 : TEXCOORD1,
  out float4 o0 : SV_Target0)
{
  float4 r0,r1,r2;
  bool outside = (v2.y < v1.x) || (v1.x < v2.z) || (v1.y > 1.0) || (v1.y < 0.0); // TODO: fix UW support? Or are the videos 21:9?
  r1.xyzw = t_image.Sample(s_image_s, v1.xy).xyzw;
  // TODO: fix BT.601? Or was it correct? How about limited/full range?
  r2.xyzw = float4(1.16406,0,-0.390625,2.01561999) * r1.zyyy;
  r0.yzw = r2.yzw + r2.x + r1.x * float3(1.59765005,-0.8125,0) + float3(-0.869610012,0.53075999,-1.07860005);
  r0.yzw *= r1.w;
  r0.yzw = sqrt(abs(r0.yzw)) * Sign_Fast(r0.yzw); // Luma: fix NaNs on negative values (near black)
  r0.xyz = outside ? 0.0 : r0.yzw; // Draw black bars
  r1.xy = v1.xy + w1.x;
  // TODO: remove dither?
  r0.w = dot(r1.xy, float2(12.9898005,78.2330017));
  r0.w = sin(r0.w);
  r0.w = 43758.5469 * r0.w;
  r0.w = frac(r0.w);
  r0.w = -0.5 + r0.w;
  r0.w = dot(r0.www, float3(0.00333333333,0.00333333333,0.00333333333));
  r0.xyz = r0.xyz + r0.www;
  o0.xyz = r0.xyz * r0.xyz;
  o0.w = 1;

  //o0.xyz = linear_to_gamma(o0.xyz);
  //o0 = saturate(o0); // Clamp
}