#include "./Includes/Common.hlsl"
// #include "./Includes/HermiteSpline_portable.hlsl"
#include "./Includes/ictcp_portable.hlsl"

#define FIRE_PEAK GS.RetunedFirePeak /* 1.26 */
#define FIRE_BOOST GS.RetunedFireBoost /* 2.6 */

/////////////////////////////////////////////////////////////////////////////////////////
// rect is top left (x,y), bottom right (x,y)
float3 DrawRect(float2 uv, float4 rect, float3 color, float3 rectColor) {
	float r = step(rect.x, uv.x) * step(uv.x, rect.z) * step(rect.y, uv.y) * step(uv.y, rect.w);
	if (r == 0) return color;
	return rectColor;
}
////////////////////////////////////////////////////////////////////////////////////////
float3 RestoreHueAndChrominanceUcsInternal(float3 targetUcs, float3 sourceUcs, float currentChrominance, float hueStrength, float chrominanceStrength, float minChromaRatio = 0.f)
{
  if (targetUcs.x == 0) return targetUcs;

  if (hueStrength != 0.0)
  {
    const float chrominancePre = currentChrominance;
    targetUcs.yz = lerp(targetUcs.yz, sourceUcs.yz, hueStrength);
    const float chrominancePost = length(targetUcs.yz);
    float chrominanceRatio = safeDivision(chrominancePre, chrominancePost, 1);
    targetUcs.yz *= chrominanceRatio;
  }

  if (chrominanceStrength != 0.0)
  {
    const float sourceChrominance = length(sourceUcs.yz);
    float targetChrominanceRatio = safeDivision(sourceChrominance, currentChrominance, 1);
    targetChrominanceRatio = clamp(targetChrominanceRatio, minChromaRatio, FLT_MAX);
    targetUcs.yz *= lerp(1.0, targetChrominanceRatio, chrominanceStrength);
  }

  return targetUcs;
}

float3 RestoreHueAndChrominanceUcs(float3 targetUcs, float3 sourceUcs, float hueStrength, float chrominanceStrength, float minChromaRatio = 0.f)
{
  return RestoreHueAndChrominanceUcsInternal(targetUcs, sourceUcs, length(targetUcs.yz), hueStrength, chrominanceStrength, minChromaRatio);
}
////////////////////////////////////////////////////////////////////////////////////////
float3 UCS_Encode(float3 x) {
  return renodx::color::ictcp::To(x, CS_BT709);
}
float3 UCS_Decode(float3 x) {
  return renodx::color::ictcp::From(x, CS_BT709);
}
/////////////////////////////////////////////////////////////////////////////////////////
// float GetLuminance(float3 x) {
//   return dot(x, float3(0.2126390059f, 0.7151686788f, 0.0721923154f));
// }
float3 ClampByMaxChannel(float3 x, float p) {
  float m = max(max(x.x, x.y), x.z);
  if (m > p) x *= p / m;
  return x;
}
bool FloatEquals(float a, float b, float epsilon) {
  return abs(a - b) <= epsilon;
}
/////////////////////////////////////////////////////////////////////////////////////////
float Neutwo(float x) {
  float numerator = x;
  float denominator_squared = mad(x, x, 1.0);
  return numerator * rsqrt(denominator_squared);
}
float Neutwo(float x, float peak) {
  float p = peak;

  float numerator = p * x;
  float denominator_squared = mad(x, x, p * p);
  return numerator * rsqrt(denominator_squared);
}
float3 Neutwo(float3 x, float peak) {
  float p = peak;

  float3 numerator = p * x;
  float3 denominator_squared = mad(x, x, p * p);
  return numerator * rsqrt(denominator_squared);
}
float3 Neupow(float3 x, float peak, float power) {
  float p = peak;
  float3 numerator = p * x;
  float3 denominator_pow = pow(x, power) + pow(p, power);
  return numerator / pow(denominator_pow, rcp(power));
}
float Neupow(float x, float peak, float power) {
  float p = peak;
  float numerator = p * x;
  float denominator_pow = pow(x, power) + pow(p, power);
  return numerator / pow(denominator_pow, rcp(power));
}

float Neutwo(float x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float xx = x * x;

  float numerator = c * p * x;
  float denominator_squared = mad(xx, (cc - pp), cc * pp);

  return numerator * rsqrt(denominator_squared);
}
float3 Neutwo(float3 x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float3 xx = x * x;

  float3 numerator = c * p * x;
  float3 denominator_squared = mad(xx, (cc - pp), cc * pp);

  return numerator * rsqrt(denominator_squared);
}
float2 Neutwo(float2 x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float2 xx = x * x;

  float2 numerator = c * p * x;
  float2 denominator_squared = mad(xx, (cc - pp), cc * pp);

  return numerator * rsqrt(denominator_squared);
}
float NeutwoI(float x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float xx = x * x;
  float numerator = c * p * x;
  float denominator_squared = mad(-xx, (cc - pp), cc * pp);
  return numerator * rsqrt(denominator_squared);
}
float3 NeutwoI(float3 x, float peak, float clip) {
  float p = peak;
  float c = clip;
  float cc = c * c;
  float pp = p * p;
  float3 xx = x * x;
  float3 numerator = c * p * x;
  float3 denominator_squared = mad(-xx, (cc - pp), cc * pp);
  return numerator * rsqrt(denominator_squared);
}
float3 Neupow(float3 x, float peak, float clip, float power) {
  float p = peak;
  float c = clip;
  float cc = pow(c, power);
  float pp = pow(p, power);
  float3 xx = pow(x, power);

  float3 numerator = c * p * x;
  float3 denominator_pow = xx * ((cc - pp)) + (cc * pp);
  return numerator / pow(denominator_pow, rcp(power));
}
float Neupow(float x, float peak, float clip, float power) {
  float p = peak;
  float c = clip;
  float cc = pow(c, power);
  float pp = pow(p, power);
  float xx = pow(x, power);

  float numerator = c * p * x;
  float denominator_pow = xx * ((cc - pp)) + (cc * pp);
  return numerator / pow(denominator_pow, rcp(power));
}
////////////////////////////////////////////////////////////////////////////////////////
float3 HDRTonemap(float3 x, float peak, float start = 0.18) {
  x = Neupow(x, peak, 1 * GS.WhiteClip);
  return x;
}
////////////////////////////////////////////////////////////////////////////////////////
float FireTonemap(float x, float clip, float output_max = 1.0f) {
  x = min(x, clip);
  x = Neutwo(x, output_max, clip);
  return x;
}
float2 FireTonemap(float2 x, float clip, float output_max = 1.0f) {
  x = min(x, clip);
  x = Neutwo(x, output_max, clip);
  return x;
}
float3 FireTonemap(float3 x, float clip, float output_max = 1.0f) {
  x = min(x, clip);
  x = Neutwo(x, output_max, clip);
  return x;
}