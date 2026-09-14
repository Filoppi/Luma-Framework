#include "./Includes/Common.hlsl"

/////////////////////////////////////////////////////////////////////////////////////////
// float GetLuminance(float3 x) {
//   return dot(x, float3(0.2126390059f, 0.7151686788f, 0.0721923154f));
// }
float3 ClampByMaxChannel(float3 x, float p) {
  float m = max(max(x.x, x.y), x.z);
  if (m > p) x *= p / m;
  return x;
}
float DivideSafe(float numerator, float denominator, float defaultValue) {
  return denominator != 0.0f ? numerator / denominator : defaultValue;
}
float3 DivideSafe(float3 numerator, float3 denominator, float3 defaultValue) {
  return denominator != 0.0f ? numerator / denominator : defaultValue;
}
bool FloatEqual(float a, float b, float epsilon) {
  return abs(a - b) < epsilon;
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
/////////////////////////////////////////////////////////////////////////////////////////
float ReinhardSimple(float x, float peak = 1.0)
{
  return x / ((abs(x) / peak) + 1.0);
}
float3 ReinhardSimple(float3 x, float peak = 1.0)
{
  return x / ((abs(x) / peak) + 1.0);
}
float ReinhardExtended(float color, float white_max = 1000.f / 203.f, float peak = 1.f) {
  return ReinhardSimple(color, peak) * (1.f + (peak * color) / (white_max * white_max));
}
float3 ReinhardExtended(float3 color, float white_max = 1000.f / 203.f, float peak = 1.f) {
  return ReinhardSimple(color, peak) * (1.f + (peak * color) / (white_max * white_max));
}
float ComputeReinhardScale(float channel_max = 1.f, float channel_min = 0.f, float gray_in = MidGray, float gray_out = MidGray) {
  return (channel_max * (channel_min * gray_out + channel_min - gray_out))
        / (gray_in * (gray_out - channel_max));
}
float3 ReinhardPiecewise(float3 x, float x_max = 1.f, float shoulder = 0.18f) {
    const float x_min = 0.f;
    float exposure = ComputeReinhardScale(x_max, x_min, shoulder, shoulder);
    float3 tonemapped = mad(x, exposure, x_min) / mad(x, exposure / x_max, 1.f - x_min);
    return lerp(x, tonemapped, step(shoulder, x));
}
float ReinhardPiecewise(float x, float x_max = 1.f, float shoulder = 0.18f) {
    const float x_min = 0.f;
    float exposure = ComputeReinhardScale(x_max, x_min, shoulder, shoulder);
    float tonemapped = mad(x, exposure, x_min) / mad(x, exposure / x_max, 1.f - x_min);
    return lerp(x, tonemapped, step(shoulder, x));
}
/////////////////////////////////////////////////////////////////////////////////////////
float3 HejlDawson(float3 x) {
  x = max(0, x - 0.004f);
  x = (x * (6.2f * x + 0.5f)) / (x * (6.2f * x + 1.7f) + 0.06f);
  return x;
}
float3 HejlDawsonHDR(float3 x) {
  // lower
  float3 l = HejlDawson(x);
  l = sRGB_Decode(l); // inverse HejlDawson embedded Gamma Encode

  // upper
  float thres = 0.260396;
    float slope = 1.;
    float output = 0.310476593788;
  float3 u = slope * (x - thres) + output;

  // extend
  x = x < thres ? l : u;

  // HejlDawson embedded Gamma Encode
  x = sRGB_Encode(x);
  return x;
}
////////////////////////////////////////////////////////////////////////////////////////
// float3 RestorePostProcess(float3 nonPostProcessedTargetColor, float3 nonPostProcessedSourceColor, float3 postProcessedSourceColor, float hueRestoration = 0.0)
// {
//   static bool BT2020 = true;
//   static const float DefaultGamma = 2.2f;
//   static const float MaxShadowsColor = pow(1.f / 3.f, DefaultGamma); // The lower this value, the more "accurate" is the restoration (math wise), but also more error prone (e.g. division by zero). If the color range is wider than the original one, the higher this value is, the further it will extend, due to working by offset near black (and thus generating negative rgb values).
// 
// 	if (BT2020)
// 	{
//     nonPostProcessedTargetColor = renodx::color::bt2020::from::BT709(nonPostProcessedTargetColor);
//     nonPostProcessedSourceColor = renodx::color::bt2020::from::BT709(nonPostProcessedSourceColor);
//     postProcessedSourceColor = renodx::color::bt2020::from::BT709(postProcessedSourceColor);
// 	}
// 
// 	const float3 postProcessColorRatio = DivideSafe(postProcessedSourceColor, nonPostProcessedSourceColor, 1);
// 	const float3 postProcessColorOffset = postProcessedSourceColor - nonPostProcessedSourceColor;
// 	const float3 postProcessedRatioColor = nonPostProcessedTargetColor * postProcessColorRatio;
// 	const float3 postProcessedOffsetColor = nonPostProcessedTargetColor + postProcessColorOffset;
// 	float3 newPostProcessedColor = lerp(postProcessedOffsetColor, postProcessedRatioColor, max(saturate(abs(nonPostProcessedTargetColor / MaxShadowsColor)), saturate(abs(nonPostProcessedSourceColor / MaxShadowsColor))));
// 
//   // if (hueRestoration > 0) 
//   // {
//   //   newPostProcessedColor = UCS_Encode(newPostProcessedColor);
//   //   postProcessedSourceColor = UCS_Encode(postProcessedSourceColor);
//   //   newPostProcessedColor = RestoreHueAndChrominanceUcs(newPostProcessedColor, postProcessedSourceColor, hueRestoration, 0.0, 0.0);
//   //   newPostProcessedColor = UCS_Decode(newPostProcessedColor);
//   // }
//   if (BT2020) 
//   {
//     newPostProcessedColor = renodx::color::bt709::from::BT2020(newPostProcessedColor);
//     newPostProcessedColor = max(newPostProcessedColor, 0);
//   }
// 
// 	return newPostProcessedColor;
// }
// float3 RestorePostProcess(float3 nonPostProcessedTargetColor, float3 nonPostProcessedSourceColor, float3 postProcessedSourceColor)
// {
//   static const float hueRestoration = 0.0;
//   static const bool BT2020 = true;
//   static const float MaxShadowsColor = pow(1.f / 3.f, DefaultGamma); // The lower this value, the more "accurate" is the restoration (math wise), but also more error prone (e.g. division by zero). If the color range is wider than the original one, the higher this value is, the further it will extend, due to working by offset near black (and thus generating negative rgb values).
// 
// 	// Optionally convert to BT.2020 to allow more saturated shadow to be generated (BT.709 will reach the edges of the gamut and clip on shadow).
//   // We could do this in AP0 gamut but that'd probably generated too many unsupported colors.
//   // This should actually distort colors less.
// 	if (BT2020)
// 	{
// 		nonPostProcessedTargetColor = BT709_To_BT2020(nonPostProcessedTargetColor);
// 		nonPostProcessedSourceColor = BT709_To_BT2020(nonPostProcessedSourceColor);
// 		postProcessedSourceColor = BT709_To_BT2020(postProcessedSourceColor);
// 	}
// 
// 	const float3 postProcessColorRatio = safeDivision(postProcessedSourceColor, nonPostProcessedSourceColor, 1);
// 	const float3 postProcessColorOffset = postProcessedSourceColor - nonPostProcessedSourceColor;
// 	const float3 postProcessedRatioColor = nonPostProcessedTargetColor * postProcessColorRatio;
// 	const float3 postProcessedOffsetColor = nonPostProcessedTargetColor + postProcessColorOffset;
// 	// Near black, we prefer using the "offset" (sum) pp restoration method, as otherwise any raised black would not work,
// 	// for example if any zero was shifted to a more raised color, "postProcessColorRatio" would not be able to replicate that shift due to a division by zero.
// 	float3 newPostProcessedColor = lerp(postProcessedOffsetColor, postProcessedRatioColor, max(saturate(abs(nonPostProcessedTargetColor / MaxShadowsColor)), saturate(abs(nonPostProcessedSourceColor / MaxShadowsColor))));
// 
// 	// // Force keep the original post processed color hue.
//   // // This often ends up shifting the hue too much, either looking too desaturated or too saturated, mostly because in SDR highlights are all burned to white by LUTs, and by the Vanilla SDR tonemappers.
// 	// if (hueRestoration > 0)
// 	// {
// 	// 	newPostProcessedColor = RestoreHueAndChrominance(newPostProcessedColor, postProcessedSourceColor, hueRestoration, 0.0, 0.0, FLT_MAX, 0.0, BT2020 ? CS_BT2020 : CS_DEFAULT);
// 	// }
// 
// 	if (BT2020)
// 	{
// 		newPostProcessedColor = BT2020_To_BT709(newPostProcessedColor);
// 	}
// 
// 	return newPostProcessedColor;
// }