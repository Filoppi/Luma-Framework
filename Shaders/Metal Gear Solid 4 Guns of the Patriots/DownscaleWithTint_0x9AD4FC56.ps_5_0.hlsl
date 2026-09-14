#include "../Includes/Common.hlsl"

#ifndef ENABLE_LUMA
#define ENABLE_LUMA 1
#endif

#ifndef ENABLE_IMPROVED_BLUR
#define ENABLE_IMPROVED_BLUR 1
#endif

Texture2D<float4> t0 : register(t0);

SamplerState s0_s : register(s0);

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

// This pass decodes RGB / alpha before averaging. SampleArea's paired bilinear
// fetches would mix encoded RGBA first, so use individual mip-0 texels here.
// Like SampleArea, integrate the complete box with fractional edge weights and
// replicate texture edges. Keep a one-texel minimum for any magnified axis.
float3 SampleAreaMGS4HDR(Texture2D<float4> source, float2 uv, float2 sourceResolution, float2 targetResolution)
{
  float2 boxSize = max(sourceResolution / targetResolution, 1.0);
  float2 boxMin = uv * sourceResolution - 0.5 * boxSize;
  float2 boxMax = boxMin + boxSize;
  int2 firstTexel = int2(floor(boxMin));
  int2 endTexel = int2(ceil(boxMax));
  int2 lastValidTexel = int2(sourceResolution) - 1;
  float3 result = 0.0;

  [loop]
  for (int y = firstTexel.y; y < endTexel.y; ++y)
  {
    float weightY = saturate(min(float(y + 1), boxMax.y) - max(float(y), boxMin.y));
    [loop]
    for (int x = firstTexel.x; x < endTexel.x; ++x)
    {
      float weightX = saturate(min(float(x + 1), boxMax.x) - max(float(x), boxMin.x));
      int2 samplePixel = clamp(int2(x, y), int2(0, 0), lastValidTexel);
      float4 encoded = source.Load(int3(samplePixel, 0));
#if 0 // Not needed, it's not anywhere else
      float3 decoded = encoded.a != 0.0 ? encoded.rgb / encoded.a : float3(0, 0, 0);
#else
      float3 decoded = encoded.rgb / encoded.a;
#endif
      result += decoded * (weightX * weightY);
    }
  }

  result /= boxSize.x * boxSize.y;

  // Do the mult part of the decode once at the end to save
  return result * 0.25;
}

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
  uint sourceWidth, sourceHeight;
  t0.GetDimensions(sourceWidth, sourceHeight);
  float2 sourceResolution = float2(sourceWidth, sourceHeight);
  float2 uvDx = ddx(v1);
  float2 uvDy = ddy(v1);
#if ENABLE_IMPROVED_BLUR
  float2 outputPixelSize = abs(uvDx) + abs(uvDy);
  float2 sourceTexelsPerPixel = outputPixelSize * sourceResolution;

  // Preserve the original four-tap filter at 1:1 (allowing interpolation roundoff)
  // and for pure upscaling. An 8x reduction instead averages all 8x8 source texels.
  [branch]
  if (any(sourceTexelsPerPixel > 1.001))
  {
    float2 targetResolution = rcp(outputPixelSize);
    r0.xyz = SampleAreaMGS4HDR(t0, v1, sourceResolution, targetResolution);
  }
  else
#endif // ENABLE_IMPROVED_BLUR
  {
    r0.xyzw = cb0[0].xyzw + v1.xyxy;
    r1.xyzw = t0.SampleGrad(s0_s, r0.zw, uvDx, uvDy).xyzw;
    r0.xyzw = t0.SampleGrad(s0_s, r0.xy, uvDx, uvDy).xyzw;
    r0.xyz = r0.xyz / r0.w;
    r1.xyz = r1.xyz / r1.w;
    r0.xyz = r0.xyz + r1.xyz;
    r1.xyzw = -cb0[0].zwxy + v1.xyxy;
    r2.xyzw = t0.SampleGrad(s0_s, r1.xy, uvDx, uvDy).xyzw;
    r1.xyzw = t0.SampleGrad(s0_s, r1.zw, uvDx, uvDy).xyzw;
    r1.xyz = r1.xyz / r1.w;
    r2.xyz = r2.xyz / r2.w;
    r0.xyz = r2.xyz + r0.xyz;
    r0.xyz = r1.xyz + r0.xyz;
    // HDR decode mult and average of 4 samples, together
    r0.xyz = r0.xyz * 0.25 * 0.25;
  }
  r1.xyz = v2.xxx * r0.xyz;
  // This is extended Reinhard but it doesn't necessarily ceil to 1 as it has parameters to push highlights further
  r2.xyz = r1.xyz * v2.yyy + float3(1,1,1);
  r1.xyz = r2.xyz * r1.xyz;
  r2.xyz = r0.xyz * v2.xxx + float3(1,1,1);
#if 1 // Luma: fix Rec.601 luminance // TODO: calculate in linear?
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
  float tonemappedLuminance = GetLuminance(gamma_to_linear(r1.xyz), GCT_NONE);
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
  o0.xyz = v5.xxx * r0.xyz;
  o0.w = 1;
}