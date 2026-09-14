#include "../Includes/Common.hlsl"

#ifndef ENABLE_LUMA
#define ENABLE_LUMA 1
#endif

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

// Values always seem to be:
// 0.000976562
// 0.000976562
// -0.000976562
// 0.000976562
// For the 1024x1024 to 256x256 downscale.
// Which makes the 4 taps accurate.
cbuffer cb0 : register(b0)
{
  float4 cb0[1];
}

// From RenoDX
float3 NeutwoRanged(float3 X, float3 ShoulderStart = MidGray, float3 PeakOut = 1.0)
{
	float3 linear_part = min(X, ShoulderStart);
	float3 shifted_x = max(0.0, X - ShoulderStart);
	float3 p = PeakOut - ShoulderStart;
	float3 numerator = p * shifted_x;
	float3 denominator_squared = mad(shifted_x, shifted_x, p * p);

	return linear_part + (numerator * rsqrt(denominator_squared));
}

// 4x downscale with HDR decode/encode
void main(
  float4 v0 : SV_POSITION0,
  float2 v1 : TEXCOORD0,
  float4 v2 : TEXCOORD1,
  float4 v3 : TEXCOORD3,
  float4 v4 : TEXCOORD4,
  float4 v5 : TEXCOORD5,
  out float4 o0 : SV_TARGET0)
{
  float4 r0,r1,r2;
  r0.xyzw = cb0[0].xyzw + v1.xyxy;
  r1.xyzw = t0.Sample(s0_s, r0.zw).xyzw;
  r0.xyzw = t0.Sample(s0_s, r0.xy).xyzw;
  r0.xyz = r0.xyz / r0.www;
  r1.xyz = r1.xyz / r1.www;
  r1.xyz = 0.25 * r1.xyz;
  r0.xyz = r0.xyz * 0.25 + r1.xyz;
  r1.xyzw = -cb0[0].zwxy + v1.xyxy;
  r2.xyzw = t0.Sample(s0_s, r1.xy).xyzw;
  r1.xyzw = t0.Sample(s0_s, r1.zw).xyzw;
  r1.xyz = r1.xyz / r1.www;
  r2.xyz = r2.xyz / r2.www;
  r0.xyz = r2.xyz * 0.25 + r0.xyz;
  r0.xyz = r1.xyz * 0.25 + r0.xyz;
  r0.xyz = 0.25 * r0.xyz;
  r1.xyz = v2.xxx * r0.xyz;
  r2.xyz = r1.xyz * v2.yyy + float3(1,1,1);
  r1.xyz = r2.xyz * r1.xyz;
  r2.xyz = r0.xyz * v2.xxx + float3(1,1,1);
#if 1 // Luma: fix Rec.601 luminance // TODO: calculate in linear space
  r0.x = GetLuminance(r0.xyz);
#else
  r0.x = dot(r0.xyz, float3(0.300000012,0.589999974,0.109999999));
#endif
  r0.x = -v3.w + r0.x;
  r0.x = max(0, r0.x);
  r0.xyz = v3.xyz * r0.xxx;
  r1.xyz = r1.xyz / r2.xyz;
  r1.xyz = r1.xyz * v4.yyy - v4.zzz;
#if ENABLE_LUMA
  r1.xyz = max(r1.xyz, 0.0);
#if 0
  float targetLuminance = GetLuminance(gamma_to_linear(saturate(r1.xyz), GCT_NONE));
  // Compress by max channel instead of raw clipping to preserve details and avoid hue shifts, this is only part of the final bloom color,
  // so unclamping completely could totally change the balance, hence this should work better.
  float peak = max3(r1.rgb);
  r1.xyz = r1.xyz / max(1.0, peak);
  float tonemappedLuminance = GetLuminance(gamma_to_linear(r1.xyz, GCT_NONE));
  // Restore the original luminance otherwise bloom can get too dim, or bright (which is fine!), depending on the method
  if (tonemappedLuminance > 1e-6)
  {
    r1.xyz *= linear_to_gamma(targetLuminance / tonemappedLuminance, GCT_NONE);
  }
#else // This method looks better, the one above increases saturation too much (see "IMPROVED_COLOR_GRADING_TYPE")
  r1.xyz = gamma_to_linear(r1.xyz);
  float targetLuminance = GetLuminance(r1.xyz);
  // Desat to match the saturate, but without loosing quality. The biggest influence is given from mip 0 anyway, not this.
  r1.xyz = NeutwoRanged(r1.xyz);
  float tonemappedLuminance = GetLuminance(r1.xyz);
  // Restore the original luminance otherwise bloom can get too dim, or bright (which is fine!), depending on the method
  if (tonemappedLuminance > 1e-6)
  {
    r1.xyz *= targetLuminance / tonemappedLuminance;
  }
  r1.xyz = linear_to_gamma(r1.xyz);
#endif
#else
  r1.xyz = saturate(r1.xyz);
#endif // ENABLE_LUMA
  r0.xyz = r1.xyz * v4.www + r0.xyz;
  r0.w = max(r0.x, r0.y);
  r1.x = max(0.25, r0.z);
  r0.w = max(r1.x, r0.w);
  r0.w = 1 / r0.w;
  // Re-encode HDR
  o0.xyz = r0.xyz * r0.w;
  o0.w = 0.25 * r0.w;
#if 0 // Luma: disable unnecessary saturate
  o0.xyz = saturate(o0.xyz);
  o0.w = saturate(o0.w);
#endif
}